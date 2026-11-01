//! 第三步：语法分析 —— 用 chumsky 组合子描述文法
//!
//! 文法（EBNF，与下面的函数一一对应）：
//!
//! ```text
//! program := rule* EOF
//! rule    := "rule" STRING ("priority" INT)? "when" expr "then" action* "end"
//! action  := "set" path "=" expr ";"
//!          | "log" "(" expr ("," expr)* ")" ";"
//!          | "reject" "(" expr ")" ";"
//! expr    := or
//! or      := and  ("||" and)*              ← 优先级最低
//! and     := cmp  ("&&" cmp)*
//! cmp     := add  (("=="|"!="|">"|">="|"<"|"<=") add)?
//! add     := mul  (("+"|"-") mul)*
//! mul     := unary (("*"|"/"|"%") unary)*
//! unary   := ("!"|"-") unary | atom        ← 优先级最高
//! atom    := INT | FLOAT | STRING | "true" | "false" | path | "(" expr ")"
//! path    := IDENT ("." IDENT)*
//! ```
//!
//! 对照 C1 教程：bison 用的是 LR(1)（表格驱动、文法写在 .y 文件里）；
//! chumsky 是递归下降的组合子库，文法直接用 Rust 代码表达。
//! 优先级/结合性的处理思路和第 4 章手写递归下降完全一样：每个优先级一层函数。
//!
//! 写法上有个工程细节：用到"当前 span"的闭包都收进签名完整的独立函数里，
//! 帮编译器把泛型参数钉死（chumsky 的 map_with 闭包在泛型上下文里推断较弱）。

use chumsky::{
    input::{Stream, ValueInput},
    prelude::*,
};

use crate::ast::*;
use crate::token::{Span, Token, unquote};

/// 错误类型用 `Rich`：它保留"期望什么、找到什么"，报错信息比 `Simple` 丰富。
/// 第三个类型参数是 Span 类型，要和输入的 Span 一致
type PErr<'a> = extra::Err<Rich<'a, Token, Span>>;

// ----------------------------------------------------------------表达式--

/// 把 `Parser<Expr>` 变成 `Parser<(Expr, Span)>`
fn spanned<'a, I>(
    p: impl Parser<'a, I, Expr, PErr<'a>> + Clone,
) -> impl Parser<'a, I, (Expr, Span), PErr<'a>> + Clone
where
    I: ValueInput<'a, Token = Token, Span = Span>,
{
    p.map_with(|out, e| (out, e.span().clone()))
}

/// `["order", "amount"]` → `Expr::Path(["order","amount"], span)`
fn path_expr<'a, I>(
    segs: impl Parser<'a, I, Vec<String>, PErr<'a>> + Clone,
) -> impl Parser<'a, I, (Expr, Span), PErr<'a>> + Clone
where
    I: ValueInput<'a, Token = Token, Span = Span>,
{
    segs.map_with(|segs, e| {
        let span = e.span().clone();
        (Expr::Path(segs, span.clone()), span)
    })
}

/// `unary := ("!"|"-") unary | atom`。前缀可叠加（!!x、--y）：
/// `repeated()` 收集所有前缀后逆序折叠，最靠近操作数的先应用
fn unary_expr<'a, I>(
    prefix: impl Parser<'a, I, UnOp, PErr<'a>> + Clone,
    atom: impl Parser<'a, I, (Expr, Span), PErr<'a>> + Clone,
) -> impl Parser<'a, I, (Expr, Span), PErr<'a>> + Clone
where
    I: ValueInput<'a, Token = Token, Span = Span>,
{
    prefix
        .repeated()
        .at_least(1)
        .collect::<Vec<_>>()
        .then(atom.clone())
        .map_with(|(ops, (inner, _)), e| {
            let span = e.span().clone();
            let expr = ops
                .into_iter()
                .rev()
                .fold(inner, |inner, op| Expr::Unary(op, Box::new(inner)));
            (expr, span)
        })
        .or(atom)
}

/// 二元运算一层：`operand (op operand)*`，左结合。
///
/// 传入/返回的都是 `(Expr, Span)`，这样折叠时能算出每个子表达式的精确范围。
fn binary_level<'a, I>(
    operand: impl Parser<'a, I, (Expr, Span), PErr<'a>> + Clone,
    ops: impl Parser<'a, I, BinOp, PErr<'a>> + Clone,
) -> impl Parser<'a, I, (Expr, Span), PErr<'a>> + Clone
where
    I: ValueInput<'a, Token = Token, Span = Span>,
{
    operand
        .clone()
        .then(ops.then(operand).repeated().collect::<Vec<_>>())
        .map_with(|(first, rest), _| {
            // 左结合折叠：1 + 2 + 3 → (1 + 2) + 3
            rest.into_iter()
                .fold(first, |(lhs, lspan), (op, (rhs, rspan))| {
                    let span = lspan.start..rspan.end;
                    let expr = Expr::Binary(op, Box::new(lhs), Box::new(rhs), span.clone());
                    (expr, span)
                })
        })
}

// ----------------------------------------------------------------动作--

fn set_action<'a, I>(
    segments: impl Parser<'a, I, Vec<String>, PErr<'a>> + Clone,
    expr: impl Parser<'a, I, (Expr, Span), PErr<'a>> + Clone,
) -> impl Parser<'a, I, Action, PErr<'a>> + Clone
where
    I: ValueInput<'a, Token = Token, Span = Span>,
{
    just(Token::Set)
        .ignore_then(segments)
        .then_ignore(just(Token::Eq))
        .then(expr)
        .then_ignore(just(Token::Semi))
        .map_with(|(path, (value, _)), e| Action::Set {
            path,
            value,
            span: e.span().clone(),
        })
}

fn log_action<'a, I>(
    expr: impl Parser<'a, I, (Expr, Span), PErr<'a>> + Clone,
) -> impl Parser<'a, I, Action, PErr<'a>> + Clone
where
    I: ValueInput<'a, Token = Token, Span = Span>,
{
    just(Token::Log)
        .ignore_then(
            expr.separated_by(just(Token::Comma))
                .at_least(1)
                .collect::<Vec<_>>()
                .delimited_by(just(Token::LParen), just(Token::RParen)),
        )
        .then_ignore(just(Token::Semi))
        .map_with(|args: Vec<(Expr, Span)>, _| Action::Log {
            args: args.into_iter().map(|(e, _)| e).collect(),
        })
}

fn reject_action<'a, I>(
    expr: impl Parser<'a, I, (Expr, Span), PErr<'a>> + Clone,
) -> impl Parser<'a, I, Action, PErr<'a>> + Clone
where
    I: ValueInput<'a, Token = Token, Span = Span>,
{
    just(Token::Reject)
        .ignore_then(expr.delimited_by(just(Token::LParen), just(Token::RParen)))
        .then_ignore(just(Token::Semi))
        .map_with(|(reason, _), _| Action::Reject { reason })
}

// ----------------------------------------------------------------规则--

fn rule_parser<'a, I>(
    expr: impl Parser<'a, I, (Expr, Span), PErr<'a>> + Clone,
    action: impl Parser<'a, I, Action, PErr<'a>> + Clone,
) -> impl Parser<'a, I, Rule, PErr<'a>> + Clone
where
    I: ValueInput<'a, Token = Token, Span = Span>,
{
    just(Token::Rule)
        .ignore_then(select! { Token::Str(raw) => unquote(&raw) }.labelled("字符串"))
        .then(
            just(Token::Priority)
                // labelled：给 select! 的失败贴上人类可读的标签，
                // 报错时"期望 整数"比"期望 某种特定 token"友好得多
                .ignore_then(select! { Token::Int(p) => p }.labelled("整数"))
                .or_not(),
        )
        .then_ignore(just(Token::When))
        .then(expr)
        .then_ignore(just(Token::Then))
        .then(action.repeated().collect::<Vec<_>>())
        .then_ignore(just(Token::End))
        .map_with(|(((name, priority), (when, _)), actions), e| Rule {
            name,
            priority: priority.unwrap_or(0),
            when,
            actions,
            span: e.span().clone(),
        })
        // 错误恢复：一条规则写坏了，就跳过若干 token 逐位重试
        // （在下一个 `rule` 关键字处重试成功），让剩下的规则还能被解析、
        // 还能继续报后续错误。这正是 bison 的 error 记号干的事（见 6.6 节）。
        .recover_with(skip_then_retry_until(any().ignored(), end()))
}

// ----------------------------------------------------------------入口--

pub fn parser<'a, I>() -> impl Parser<'a, I, Program, PErr<'a>> + Clone
where
    I: ValueInput<'a, Token = Token, Span = Span>,
{
    // 字面量
    let lit = choice((
        select! { Token::Int(n) => Expr::Int(n) },
        select! { Token::Float(f) => Expr::Float(f) },
        select! { Token::Str(raw) => Expr::Str(unquote(&raw)) },
        just(Token::True).to(Expr::Bool(true)),
        just(Token::False).to(Expr::Bool(false)),
    ));

    // 事实路径的段序列：order.amount → ["order", "amount"]
    let segments = select! { Token::Ident(name) => name }
        .separated_by(just(Token::Dot))
        .at_least(1)
        .collect::<Vec<_>>();

    // 表达式：递归（括号里还能出现完整表达式），从低到高逐层组合
    let expr = recursive(|expr| {
        let atom = choice((
            spanned(lit),
            path_expr(segments.clone()),
            expr.clone()
                .delimited_by(just(Token::LParen), just(Token::RParen)),
        ));

        let prefix = choice((
            just(Token::Bang).to(UnOp::Not),
            just(Token::Minus).to(UnOp::Neg),
        ));
        let unary = unary_expr(prefix, atom);

        let mul = binary_level(
            unary,
            choice((
                just(Token::Star).to(BinOp::Mul),
                just(Token::Slash).to(BinOp::Div),
                just(Token::Percent).to(BinOp::Rem),
            )),
        );
        let add = binary_level(
            mul,
            choice((
                just(Token::Plus).to(BinOp::Add),
                just(Token::Minus).to(BinOp::Sub),
            )),
        );
        let cmp = binary_level(
            add,
            choice((
                just(Token::EqEq).to(BinOp::Eq),
                just(Token::NotEq).to(BinOp::Ne),
                just(Token::Ge).to(BinOp::Ge),
                just(Token::Le).to(BinOp::Le),
                just(Token::Gt).to(BinOp::Gt),
                just(Token::Lt).to(BinOp::Lt),
            )),
        );
        let and = binary_level(cmp, just(Token::AndAnd).to(BinOp::And));
        binary_level(and, just(Token::OrOr).to(BinOp::Or))
    });

    let action = choice((
        set_action(segments.clone(), expr.clone()),
        log_action(expr.clone()),
        reject_action(expr.clone()),
    ));
    let rule = rule_parser(expr, action);

    rule.repeated()
        .collect::<Vec<_>>()
        .then_ignore(end())
        .map(|rules| Program { rules })
}

/// 入口：token 流 → (AST, 所有语法错误)。
/// 即使有错误也尽量返回能解析出的部分 AST —— 这就是"恢复"的意义。
pub fn parse(
    tokens: Vec<(Token, Span)>,
    eoi: Span,
) -> (Option<Program>, Vec<Rich<'static, Token, Span>>) {
    // Stream 把迭代器变成 chumsky 能回溯的输入；
    // .map(eoi, ...) 把 (Token, Span) 拆成"token + 位置"两栏（chumsky 0.10 的固定用法）
    let input = Stream::from_iter(tokens).map(eoi, |(t, s)| (t, s));
    let (ast, errs) = parser().parse(input).into_output_errors();
    // Rich 里的生命周期和 token 借用有关，into_owned 拿到独立于输入的错误
    (ast, errs.into_iter().map(|e| e.into_owned()).collect())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::token::lex;

    fn parse_src(src: &str) -> (Option<Program>, Vec<Rich<'static, Token, Span>>) {
        let toks = lex(src).expect("lex error");
        let eoi = toks.last().map(|(_, s)| s.end..s.end).unwrap_or(0..0);
        parse(toks, eoi)
    }

    #[test]
    fn precedence_arith_higher_than_cmp() {
        // 1 + 2 * 3 < 10 应解析为 (1 + (2*3)) < 10
        let src = r#"rule "t" when 1 + 2 * 3 < 10 then end"#;
        let (ast, errs) = parse_src(src);
        assert!(errs.is_empty(), "{errs:?}");
        match &ast.unwrap().rules[0].when {
            Expr::Binary(BinOp::Lt, lhs, _, _) => match lhs.as_ref() {
                Expr::Binary(BinOp::Add, _, rhs, _) => match rhs.as_ref() {
                    Expr::Binary(BinOp::Mul, _, _, _) => {}
                    other => panic!("rhs should be Mul, got {other:?}"),
                },
                other => panic!("lhs should be Add, got {other:?}"),
            },
            other => panic!("root should be Lt, got {other:?}"),
        }
    }

    #[test]
    fn and_binds_tighter_than_or() {
        // a || b && c → a || (b && c)
        let src = r#"rule "t" when a || b && c then end"#;
        let (ast, errs) = parse_src(src);
        assert!(errs.is_empty(), "{errs:?}");
        match &ast.unwrap().rules[0].when {
            Expr::Binary(BinOp::Or, _, rhs, _) => match rhs.as_ref() {
                Expr::Binary(BinOp::And, _, _, _) => {}
                other => panic!("rhs should be And, got {other:?}"),
            },
            other => panic!("root should be Or, got {other:?}"),
        }
    }

    #[test]
    fn recover_from_bad_rule_and_keep_good_one() {
        // 第一条规则 priority 后缺了数字 → 出错；
        // 恢复机制应跳过它，继续解析出第二条
        let src = r#"
            rule "坏" priority when then end
            rule "好" when true then end
        "#;
        let (ast, errs) = parse_src(src);
        assert!(!errs.is_empty(), "应该报语法错误");
        let ast = ast.expect("恢复后应能解析出部分 AST");
        assert_eq!(ast.rules.len(), 1);
        assert_eq!(ast.rules[0].name, "好");
    }

    #[test]
    fn unary_and_paren() {
        let src = r#"rule "t" when !(a >= 1) && -b == 2 then end"#;
        let (ast, errs) = parse_src(src);
        assert!(errs.is_empty(), "{errs:?}");
        assert!(ast.is_some());
    }

    #[test]
    fn actions_parse() {
        let src = r#"
            rule "r" priority 3 when order.amount >= 200 then
                set order.discount = 0.05;
                log("满减生效, 金额=", order.amount);
                reject("测试");
            end
        "#;
        let (ast, errs) = parse_src(src);
        assert!(errs.is_empty(), "{errs:?}");
        let rule = &ast.unwrap().rules[0];
        assert_eq!(rule.priority, 3);
        assert_eq!(rule.actions.len(), 3);
    }
}
