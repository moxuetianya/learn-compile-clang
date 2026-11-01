//! 第一步：词法分析 —— 用 logos 声明 Token
//!
//! logos 会为每个变体生成 DFA，按"最长匹配"切词。
//! 等长匹配时字面量 token（关键字、运算符）优先于 `Ident` 正则，
//! 所以 `rule` 会被识别成 `Token::Rule` 而不是标识符。
//!
//! 对照 C1 教程：这里做的事和 `src/lexer/c1.l`（flex）完全一样，
//! 只是"正则 → 自动机"由过程宏在编译期完成，不需要单独跑生成器。

use logos::Logos;
use std::ops::Range;

/// 每个 token 在源码中的位置，后面 ariadne 报错全靠它
pub type Span = Range<usize>;

#[derive(Logos, Debug, Clone, PartialEq)]
#[logos(skip r"[ \t\r\n\f]+")] // 空白直接跳过（flex 里相当于 [ \t\n] ;）
#[logos(skip r"#.*")] // # 到行尾是注释
pub enum Token {
    // ---- 关键字 ----
    #[token("rule")]
    Rule,
    #[token("priority")]
    Priority,
    #[token("when")]
    When,
    #[token("then")]
    Then,
    #[token("end")]
    End,
    #[token("set")]
    Set,
    #[token("log")]
    Log,
    #[token("reject")]
    Reject,
    #[token("true")]
    True,
    #[token("false")]
    False,

    // ---- 字面量（回调里完成 解析/收集，失败会变成词法错误） ----
    /// 整数，如 `42`。整数溢出时回调返回 None → 该 token 成为词法错误
    #[regex(r"[0-9]+", |lex| lex.slice().parse::<i64>().ok())]
    Int(i64),
    /// 浮点数，如 `0.15`。注意 `3.14` 整体匹配 Float（最长匹配胜出）
    #[regex(r"[0-9]+\.[0-9]+", |lex| lex.slice().parse::<f64>().ok())]
    Float(f64),
    /// 字符串字面量，先存原始文本（带引号），解析后再反转义
    #[regex(r#""([^"\\]|\\.)*""#, |lex| lex.slice().to_string())]
    Str(String),

    // ---- 标识符：事实路径的一段（order、amount……） ----
    #[regex(r"[a-zA-Z_][a-zA-Z0-9_]*", |lex| lex.slice().to_string())]
    Ident(String),

    // ---- 运算符与标点（多字符的必须放前面吗？不用，logos 按最长匹配） ----
    #[token("&&")]
    AndAnd,
    #[token("||")]
    OrOr,
    #[token("==")]
    EqEq,
    #[token("!=")]
    NotEq,
    #[token(">=")]
    Ge,
    #[token("<=")]
    Le,
    #[token(">")]
    Gt,
    #[token("<")]
    Lt,
    #[token("+")]
    Plus,
    #[token("-")]
    Minus,
    #[token("*")]
    Star,
    #[token("/")]
    Slash,
    #[token("%")]
    Percent,
    #[token("!")]
    Bang,
    #[token("=")]
    Eq,
    #[token("(")]
    LParen,
    #[token(")")]
    RParen,
    #[token(",")]
    Comma,
    #[token(";")]
    Semi,
    #[token(".")]
    Dot,
}

/// 把源码切成 `(Token, Span)` 序列；遇到无法识别的字符立即报错
pub fn lex(src: &str) -> Result<Vec<(Token, Span)>, (String, Span)> {
    let mut tokens = Vec::new();
    for (tok, span) in Token::lexer(src).spanned() {
        match tok {
            Ok(t) => tokens.push((t, span)),
            Err(()) => return Err((src[span.clone()].to_string(), span)),
        }
    }
    Ok(tokens)
}

/// 人类可读的名字，语法错误的"期望 X / 找到 Y"里用
/// （外层会统一包反引号，这里给裸名字）
impl std::fmt::Display for Token {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        use Token::*;
        match self {
            Rule => f.write_str("rule"),
            Priority => f.write_str("priority"),
            When => f.write_str("when"),
            Then => f.write_str("then"),
            End => f.write_str("end"),
            Set => f.write_str("set"),
            Log => f.write_str("log"),
            Reject => f.write_str("reject"),
            True => f.write_str("true"),
            False => f.write_str("false"),
            Int(n) => write!(f, "整数 {n}"),
            Float(x) => write!(f, "浮点数 {x}"),
            Str(s) => write!(f, "字符串 \"{s}\""),
            Ident(s) => write!(f, "标识符 {s}"),
            AndAnd => f.write_str("&&"),
            OrOr => f.write_str("||"),
            EqEq => f.write_str("=="),
            NotEq => f.write_str("!="),
            Ge => f.write_str(">="),
            Le => f.write_str("<="),
            Gt => f.write_str(">"),
            Lt => f.write_str("<"),
            Plus => f.write_str("+"),
            Minus => f.write_str("-"),
            Star => f.write_str("*"),
            Slash => f.write_str("/"),
            Percent => f.write_str("%"),
            Bang => f.write_str("!"),
            Eq => f.write_str("="),
            LParen => f.write_str("("),
            RParen => f.write_str(")"),
            Comma => f.write_str(","),
            Semi => f.write_str(";"),
            Dot => f.write_str("."),
        }
    }
}

/// 去掉引号并处理常见转义：`"a\"b\n"` → `a"b<换行>`
pub fn unquote(raw: &str) -> String {
    let body = &raw[1..raw.len() - 1];
    let mut out = String::with_capacity(body.len());
    let mut chars = body.chars();
    while let Some(c) = chars.next() {
        if c != '\\' {
            out.push(c);
            continue;
        }
        match chars.next() {
            Some('n') => out.push('\n'),
            Some('t') => out.push('\t'),
            Some('r') => out.push('\r'),
            Some('"') => out.push('"'),
            Some('\\') => out.push('\\'),
            Some(other) => {
                out.push('\\');
                out.push(other);
            }
            None => out.push('\\'),
        }
    }
    out
}
