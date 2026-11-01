//! 第五步：错误报告 —— ariadne 把 Span 渲染成带下划线的源码定位
//!
//! 对照 C1 教程：flex/bison 年代我们只能打印 "line N: syntax error"；
//! 现代 DSL 工具链的标准是直接画出错的那一段源码。

use ariadne::{Color, Label, Report, ReportKind, Source};
use chumsky::prelude::Rich;

use crate::error::RuntimeError;
use crate::token::{Span, Token};

/// ariadne 的偏移按【字符】计（其文档明说 character offsets），
/// 而 logos 给的 span 是【字节】偏移——源码里有中文时两者不一致，
/// 不转换的话下划线会画歪。这是多字节源码工具链的经典坑。
fn to_char_span(src: &str, byte_span: &Span) -> Span {
    let start = src[..byte_span.start].chars().count();
    let end = start + src[byte_span.start..byte_span.end].chars().count();
    start..end
}

fn emit(src: &str, file: &str, span: &Span, code: &str, msg: String, label: String) {
    let span = to_char_span(src, span);
    Report::build(ReportKind::Error, (file, span.clone()))
        .with_code(code)
        .with_message(msg)
        .with_label(
            Label::new((file, span))
                .with_message(label)
                .with_color(Color::Red),
        )
        .finish()
        .print((file, Source::from(src.to_owned())))
        .expect("打印诊断信息失败");
}

pub fn print_lex_error(src: &str, file: &str, bad: &str, span: &Span) {
    emit(
        src,
        file,
        span,
        "E0001",
        format!("无法识别的字符 `{bad}`"),
        "不是合法 token 的开头".into(),
    );
}

/// 语法错误可能有多个（恢复的意义），逐条打印
pub fn print_parse_errors(src: &str, file: &str, errs: &[Rich<'static, Token, Span>]) {
    for e in errs {
        let expected: Vec<String> = e.expected().map(pattern_text).collect();
        let expected = if expected.is_empty() {
            "输入结束".to_string()
        } else {
            expected.join(" 或 ")
        };
        let found = match e.found() {
            Some(tok) => format!("{tok}"),
            None => "输入结束".to_string(),
        };
        emit(
            src,
            file,
            e.span(),
            "E0002",
            format!("语法错误：期望 {expected}，实际找到 {found}"),
            "这里不对".into(),
        );
    }
}

/// 把 chumsky 的"期望模式"翻译成可读文本（RichPattern 自己的 Display 引号套引号，太丑）
fn pattern_text(p: &chumsky::error::RichPattern<'static, Token>) -> String {
    use chumsky::error::RichPattern::*;
    match p {
        // Maybe<Token, &Token>：clone 成 owned 后 into_inner 拿到 Token
        Token(t) => format!("`{}`", t.clone().into_inner()),
        Label(s) => format!("`{s}`"),
        Identifier(s) => format!("`{s}`"),
        Any => "任意 token".into(),
        SomethingElse => "某种特定 token".into(),
        EndOfInput => "输入结束".into(),
    }
}

pub fn print_runtime_error(src: &str, file: &str, err: &RuntimeError) {
    emit(
        src,
        file,
        err.span(),
        "E0003",
        err.to_string(),
        "这里".into(),
    );
}
