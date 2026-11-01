//! 第四步：执行引擎 —— 树遍历求值（tree-walking interpreter）
//!
//! 对照 C1 教程：C1 走到 codegen 把 AST 翻成 LLVM IR 再生成机器码；
//! DSL 的"执行"就是拿着 AST 直接递归求值 —— 规模小、好调试，
//! 这也是第 13 章说的"解释执行"路线，只是 IR 换成了 AST 本身。
//!
//! 数据模型直接复用 `serde_json::Value` 当"事实"（facts），
//! 这正是规则引擎的常见形态：输入一段 JSON，规则读它、改它。

use serde_json::{Map, Number, Value};

use crate::ast::*;
use crate::error::RuntimeError;
use crate::token::Span;

/// 一次运行的汇总
#[derive(Debug, Default)]
pub struct RunReport {
    /// 按执行顺序记录命中的规则名
    pub fired: Vec<String>,
    /// log 动作收集的输出（带规则名前缀）
    pub logs: Vec<String>,
    /// reject 动作的原因（一旦 reject，后续规则不再执行）
    pub rejected: Option<String>,
}

/// 执行整个规则集，`facts` 会被就地修改（set 的效果）
pub fn run(program: &Program, facts: &mut Value) -> Result<RunReport, RuntimeError> {
    let mut report = RunReport::default();

    // 按优先级从高到低调度。sort_by 是稳定的，同优先级保持书写顺序
    let mut rules: Vec<&Rule> = program.rules.iter().collect();
    rules.sort_by(|a, b| {
        b.priority
            .cmp(&a.priority)
            .then(a.span.start.cmp(&b.span.start))
    });

    for rule in rules {
        match eval(&rule.when, facts)? {
            Value::Bool(true) => {}
            Value::Bool(false) => continue,
            other => {
                return Err(RuntimeError::CondNotBool {
                    rule: rule.name.clone(),
                    ty: type_name(&other),
                    span: rule.span.clone(),
                });
            }
        }

        report.fired.push(rule.name.clone());
        for action in &rule.actions {
            match action {
                Action::Set { path, value, span } => {
                    let v = eval(value, facts)?;
                    set_path(facts, path, v, span)?;
                }
                Action::Log { args } => {
                    let mut line = String::new();
                    for (i, arg) in args.iter().enumerate() {
                        if i > 0 {
                            line.push(' ');
                        }
                        line.push_str(&display_value(&eval(arg, facts)?));
                    }
                    report.logs.push(format!("[{}] {}", rule.name, line));
                }
                Action::Reject { reason } => {
                    let why = display_value(&eval(reason, facts)?);
                    report.rejected = Some(why.clone());
                    report.logs.push(format!("[{}] reject: {}", rule.name, why));
                    return Ok(report); // 终止整个执行
                }
            }
        }
    }
    Ok(report)
}

// ----------------------------------------------------------------求值--

pub fn eval(e: &Expr, facts: &Value) -> Result<Value, RuntimeError> {
    match e {
        Expr::Int(n) => Ok(Value::Number((*n).into())),
        // 正则保证词法层只会产出有限 f64，这里可以直接 expect
        Expr::Float(f) => Ok(Value::Number(Number::from_f64(*f).expect("有限 f64"))),
        Expr::Str(s) => Ok(Value::String(s.clone())),
        Expr::Bool(b) => Ok(Value::Bool(*b)),
        Expr::Path(segs, span) => lookup_path(facts, segs, span),
        Expr::Unary(op, inner) => {
            let v = eval(inner, facts)?;
            match (op, v) {
                (UnOp::Not, Value::Bool(b)) => Ok(Value::Bool(!b)),
                (UnOp::Neg, Value::Number(n)) => {
                    if let Some(i) = n.as_i64() {
                        Ok(Value::Number((-i).into()))
                    } else {
                        Ok(Value::Number(
                            Number::from_f64(-n.as_f64().unwrap()).expect("有限 f64"),
                        ))
                    }
                }
                (op, v) => Err(RuntimeError::BadUnOp {
                    op: if matches!(op, UnOp::Not) { "!" } else { "-" },
                    ty: type_name(&v),
                    span: expr_span(e),
                }),
            }
        }
        Expr::Binary(op, lhs, rhs, span) => {
            // && 和 || 短路求值：右边根本不用算
            if matches!(op, BinOp::And | BinOp::Or) {
                let l = eval(lhs, facts)?;
                let l = l.as_bool().ok_or_else(|| RuntimeError::BadBinOp {
                    op: op.symbol(),
                    lhs: type_name(&l),
                    rhs: "布尔值".into(),
                    span: span.clone(),
                })?;
                let decide_early = matches!((op, l), (BinOp::And, false) | (BinOp::Or, true));
                if decide_early {
                    return Ok(Value::Bool(l));
                }
                let r = eval(rhs, facts)?;
                let r = r.as_bool().ok_or_else(|| RuntimeError::BadBinOp {
                    op: op.symbol(),
                    lhs: "布尔值".into(),
                    rhs: type_name(&r),
                    span: span.clone(),
                })?;
                return Ok(Value::Bool(r));
            }

            let l = eval(lhs, facts)?;
            let r = eval(rhs, facts)?;
            match op {
                BinOp::Eq => Ok(Value::Bool(values_equal(&l, &r))),
                BinOp::Ne => Ok(Value::Bool(!values_equal(&l, &r))),
                BinOp::Gt | BinOp::Ge | BinOp::Lt | BinOp::Le => {
                    let ord = compare(&l, &r).ok_or_else(|| RuntimeError::BadBinOp {
                        op: op.symbol(),
                        lhs: type_name(&l),
                        rhs: type_name(&r),
                        span: span.clone(),
                    })?;
                    let b = match op {
                        BinOp::Gt => ord == std::cmp::Ordering::Greater,
                        BinOp::Ge => ord != std::cmp::Ordering::Less,
                        BinOp::Lt => ord == std::cmp::Ordering::Less,
                        BinOp::Le => ord != std::cmp::Ordering::Greater,
                        _ => unreachable!(),
                    };
                    Ok(Value::Bool(b))
                }
                BinOp::Add => {
                    // 数 + 数；字符串拼接（小 DSL 常见的运算符重载）
                    match (&l, &r) {
                        (Value::String(a), Value::String(b)) => {
                            Ok(Value::String(format!("{a}{b}")))
                        }
                        _ => arith(*op, &l, &r, span),
                    }
                }
                BinOp::Sub | BinOp::Mul | BinOp::Div | BinOp::Rem => arith(*op, &l, &r, span),
                BinOp::And | BinOp::Or => unreachable!(),
            }
        }
    }
}

/// 四则运算：两个整数算出整数，有浮点就是浮点（设计决策，见教程 15.6 节）
fn arith(op: BinOp, l: &Value, r: &Value, span: &Span) -> Result<Value, RuntimeError> {
    let bad = || RuntimeError::BadBinOp {
        op: op.symbol(),
        lhs: type_name(l),
        rhs: type_name(r),
        span: span.clone(),
    };
    if !(l.is_number() && r.is_number()) {
        return Err(bad());
    }
    match (l.as_i64(), r.as_i64()) {
        (Some(a), Some(b)) => {
            let v = match op {
                BinOp::Add => a.checked_add(b),
                BinOp::Sub => a.checked_sub(b),
                BinOp::Mul => a.checked_mul(b),
                BinOp::Div => {
                    if b == 0 {
                        return Err(RuntimeError::DivByZero { span: span.clone() });
                    }
                    Some(a / b) // 整数除法（向零截断）
                }
                BinOp::Rem => {
                    if b == 0 {
                        return Err(RuntimeError::DivByZero { span: span.clone() });
                    }
                    Some(a % b)
                }
                _ => unreachable!(),
            };
            v.map(|v| Value::Number(v.into()))
                .ok_or(RuntimeError::DivByZero { span: span.clone() }) // 溢出也按"算不了"处理
        }
        _ => {
            let (a, b) = (l.as_f64().unwrap(), r.as_f64().unwrap());
            let v = match op {
                BinOp::Add => a + b,
                BinOp::Sub => a - b,
                BinOp::Mul => a * b,
                BinOp::Div => {
                    if b == 0.0 {
                        return Err(RuntimeError::DivByZero { span: span.clone() });
                    }
                    a / b
                }
                BinOp::Rem => {
                    if b == 0.0 {
                        return Err(RuntimeError::DivByZero { span: span.clone() });
                    }
                    a % b
                }
                _ => unreachable!(),
            };
            Number::from_f64(v)
                .map(Value::Number)
                .ok_or(RuntimeError::DivByZero { span: span.clone() })
        }
    }
}

/// 相等：数值跨 int/float 比较（1 == 1.0 为 true），其余按 JSON 结构比较
fn values_equal(l: &Value, r: &Value) -> bool {
    if l.is_number() && r.is_number() {
        l.as_f64() == r.as_f64()
    } else {
        l == r
    }
}

/// 大小比较：数值用 f64，字符串按字典序
fn compare(l: &Value, r: &Value) -> Option<std::cmp::Ordering> {
    match (l, r) {
        (Value::String(a), Value::String(b)) => Some(a.cmp(b)),
        _ if l.is_number() && r.is_number() => {
            l.as_f64().unwrap().partial_cmp(&r.as_f64().unwrap())
        }
        _ => None,
    }
}

// ----------------------------------------------------------事实读写--

/// `order.amount` → 依次下钻 facts["order"]["amount"]；任一层缺失即报错
fn lookup_path(facts: &Value, path: &[String], span: &Span) -> Result<Value, RuntimeError> {
    let mut cur = facts;
    for seg in path {
        match cur.get(seg) {
            Some(v) => cur = v,
            None => {
                return Err(RuntimeError::UndefinedPath {
                    path: path.join("."),
                    span: span.clone(),
                });
            }
        }
    }
    Ok(cur.clone())
}

/// `set order.discount = v` → 下钻到倒数第二层（自动创建中间对象），写入末段
fn set_path(root: &mut Value, path: &[String], v: Value, span: &Span) -> Result<(), RuntimeError> {
    if path.is_empty() {
        return Ok(());
    }
    let mut cur = root;
    for seg in &path[..path.len() - 1] {
        if !cur.is_object() {
            return Err(RuntimeError::NotAnObject {
                seg: seg.clone(),
                span: span.clone(),
            });
        }
        let obj = cur.as_object_mut().unwrap();
        cur = obj
            .entry(seg.clone())
            .or_insert_with(|| Value::Object(Map::new()));
    }
    let obj = cur
        .as_object_mut()
        .ok_or_else(|| RuntimeError::NotAnObject {
            seg: path[path.len() - 1].clone(),
            span: span.clone(),
        })?;
    obj.insert(path[path.len() - 1].clone(), v);
    Ok(())
}

// -------------------------------------------------------------杂项--

/// 找到 Expr 自带的 span（一元/二元/路径有；字面量没有，返回空 span）
fn expr_span(e: &Expr) -> Span {
    match e {
        Expr::Path(_, s) => s.clone(),
        Expr::Binary(_, _, _, s) => s.clone(),
        _ => 0..0,
    }
}

/// 值的类型名，报错用
fn type_name(v: &Value) -> String {
    match v {
        Value::Null => "null".into(),
        Value::Bool(_) => "布尔值".into(),
        Value::Number(n) => {
            if n.is_i64() || n.is_u64() {
                "整数".into()
            } else {
                "浮点数".into()
            }
        }
        Value::String(_) => "字符串".into(),
        Value::Array(_) => "数组".into(),
        Value::Object(_) => "对象".into(),
    }
}

/// log 输出的展示形式：字符串去引号，其余按 JSON
fn display_value(v: &Value) -> String {
    match v {
        Value::String(s) => s.clone(),
        other => other.to_string(),
    }
}
