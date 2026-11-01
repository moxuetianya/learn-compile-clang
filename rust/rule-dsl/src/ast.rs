//! 第二步：抽象语法树 —— 语法分析的产物
//!
//! 和 C1 的 AST（`src/ast/`）相比少了变量/函数/控制流，
//! 多了规则引擎特有的东西：Rule（规则）、Action（动作）。
//! 注意我们把 Span 直接存进节点，运行期报错（如除零）才有位置可指。

use crate::token::Span;

/// 整个规则文件
#[derive(Debug)]
pub struct Program {
    pub rules: Vec<Rule>,
}

/// 一条规则：`rule "名字" priority 优先级 when 条件 then 动作* end`
#[derive(Debug)]
pub struct Rule {
    pub name: String,
    /// 越大越先执行；缺省为 0。同优先级按出现顺序（stable sort 保证）
    pub priority: i64,
    /// 触发条件，必须是布尔值
    pub when: Expr,
    pub actions: Vec<Action>,
    pub span: Span,
}

/// 表达式：条件里和 set 右边出现的都是它
#[derive(Debug, Clone)]
pub enum Expr {
    Int(i64),
    Float(f64),
    Str(String),
    Bool(bool),
    /// 事实路径：`order.amount` → ["order", "amount"]；单个 `now` → ["now"]
    Path(Vec<String>, Span),
    Unary(UnOp, Box<Expr>),
    /// 二元运算。末尾的 Span 是整个子表达式的范围，用于运行期报错
    Binary(BinOp, Box<Expr>, Box<Expr>, Span),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UnOp {
    /// `!x`
    Not,
    /// `-x`
    Neg,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BinOp {
    Or,  // ||
    And, // &&
    Eq,  // ==
    Ne,  // !=
    Ge,  // >=
    Le,  // <=
    Gt,  // >
    Lt,  // <
    Add, // +
    Sub, // -
    Mul, // *
    Div, // /
    Rem, // %
}

impl BinOp {
    /// 报错信息里用的符号形式
    pub fn symbol(self) -> &'static str {
        match self {
            BinOp::Or => "||",
            BinOp::And => "&&",
            BinOp::Eq => "==",
            BinOp::Ne => "!=",
            BinOp::Ge => ">=",
            BinOp::Le => "<=",
            BinOp::Gt => ">",
            BinOp::Lt => "<",
            BinOp::Add => "+",
            BinOp::Sub => "-",
            BinOp::Mul => "*",
            BinOp::Div => "/",
            BinOp::Rem => "%",
        }
    }
}

/// 规则命中后执行的动作
#[derive(Debug)]
pub enum Action {
    /// `set order.discount = 0.15;` —— 往事实里写值
    Set {
        path: Vec<String>,
        value: Expr,
        span: Span,
    },
    /// `log("...", expr, ...);` —— 打日志
    Log { args: Vec<Expr> },
    /// `reject("原因");` —— 终止整个规则执行，标记拒绝
    Reject { reason: Expr },
}
