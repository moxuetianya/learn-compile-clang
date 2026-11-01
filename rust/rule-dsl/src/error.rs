//! 运行期错误：都带 Span，交给 ariadne 渲染后能精确指回源码

use crate::token::Span;

#[derive(Debug, thiserror::Error)]
pub enum RuntimeError {
    #[error("未定义的事实路径 `{path}`（when/set 里引用了事实里不存在的字段）")]
    UndefinedPath { path: String, span: Span },

    #[error("运算符 `{op}` 不支持 {lhs} {op} {rhs}")]
    BadBinOp {
        op: &'static str,
        lhs: String,
        rhs: String,
        span: Span,
    },

    #[error("一元运算符 `{op}` 不支持操作数类型 {ty}")]
    BadUnOp {
        op: &'static str,
        ty: String,
        span: Span,
    },

    #[error("除以零")]
    DivByZero { span: Span },

    #[error("set 的路径中间字段 `{seg}` 不是对象，无法继续下钻")]
    NotAnObject { seg: String, span: Span },

    #[error("规则 `{rule}` 的 when 条件必须是布尔值，实际得到 {ty}")]
    CondNotBool {
        rule: String,
        ty: String,
        span: Span,
    },
}

impl RuntimeError {
    /// 错误对应的源码位置（ariadne 画下划线用）
    pub fn span(&self) -> &Span {
        match self {
            RuntimeError::UndefinedPath { span, .. }
            | RuntimeError::BadBinOp { span, .. }
            | RuntimeError::BadUnOp { span, .. }
            | RuntimeError::DivByZero { span }
            | RuntimeError::NotAnObject { span, .. }
            | RuntimeError::CondNotBool { span, .. } => span,
        }
    }
}
