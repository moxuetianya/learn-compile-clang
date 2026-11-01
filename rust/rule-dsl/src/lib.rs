//! # rule-dsl —— 用 Rust 实现的规则引擎外部 DSL
//!
//! 完整流水线（教程第 15 章的配套代码）：
//!
//! ```text
//! .rules 源码 ──logos──→ tokens ──chumsky──→ AST ──engine──→ 修改后的 facts
//!    (词法)      ≈ flex        (语法)      ≈ bison   (树遍历求值) ≈ 解释执行
//! ```
//!
//! - 词法：[`token`]（logos 过程宏生成 DFA）
//! - 语法：[`parser`]（chumsky 组合子 + 错误恢复）
//! - 执行：[`engine`]（树遍历求值，facts 用 `serde_json::Value`）
//! - 诊断：[`report`]（ariadne 渲染源码定位）
//!
//! 运行示例：
//!
//! ```bash
//! cargo run -- rules/orders.rules facts/vip-big.json
//! ```

pub mod ast;
pub mod engine;
pub mod error;
pub mod parser;
pub mod report;
pub mod token;
