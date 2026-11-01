//! rule-dsl 命令行入口：串起 词法 → 语法 → 执行 的完整流水线

use std::process::ExitCode;

use serde_json::Value;

use rule_dsl::{engine, parser, report, token};

const USAGE: &str = "\
用法: rule-dsl <rules 文件> <facts JSON 文件> [--dump-ast]

  rules 文件    规则集（本 DSL 的源码）
  facts JSON    输入事实，规则执行后会打印修改后的结果
  --dump-ast    只做词法+语法分析，打印 AST 后退出（对照 c1c 打印 AST 的玩法）
";

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let dump_ast = args.iter().any(|a| a == "--dump-ast");
    let files: Vec<&String> = args.iter().filter(|a| !a.starts_with('-')).collect();

    let [rules_path, facts_path] = files.as_slice() else {
        eprint!("{USAGE}");
        return ExitCode::from(2);
    };

    // ---- 读源码，词法分析 ----
    let src = match std::fs::read_to_string(rules_path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("读取 {rules_path} 失败: {e}");
            return ExitCode::from(2);
        }
    };
    let tokens = match token::lex(&src) {
        Ok(t) => t,
        Err((bad, span)) => {
            report::print_lex_error(&src, rules_path, &bad, &span);
            return ExitCode::from(1);
        }
    };

    // ---- 语法分析（含错误恢复） ----
    let eoi = tokens.last().map(|(_, s)| s.end..s.end).unwrap_or(0..0);
    let (ast, errs) = parser::parse(tokens, eoi);
    if !errs.is_empty() {
        report::print_parse_errors(&src, rules_path, &errs);
    }
    let Some(program) = ast else {
        eprintln!("共 {} 个语法错误，且没有任何规则被成功解析", errs.len());
        return ExitCode::from(1);
    };
    if !errs.is_empty() {
        eprintln!(
            "（{} 个语法错误；错误恢复保住了 {} 条规则，继续执行）",
            errs.len(),
            program.rules.len()
        );
    }

    if dump_ast {
        println!("{program:#?}");
        return ExitCode::SUCCESS;
    }

    // ---- 读事实（JSON），执行 ----
    let facts_src = match std::fs::read_to_string(facts_path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("读取 {facts_path} 失败: {e}");
            return ExitCode::from(2);
        }
    };
    let mut facts: Value = match serde_json::from_str(&facts_src) {
        Ok(v) => v,
        Err(e) => {
            eprintln!("facts 不是合法 JSON: {e}");
            return ExitCode::from(2);
        }
    };

    let run = match engine::run(&program, &mut facts) {
        Ok(r) => r,
        Err(e) => {
            report::print_runtime_error(&src, rules_path, &e);
            return ExitCode::from(1);
        }
    };

    // ---- 打印结果 ----
    println!("== 规则命中（按优先级执行顺序） ==");
    if run.fired.is_empty() {
        println!("（无规则命中）");
    } else {
        for name in &run.fired {
            println!("  ✓ {name}");
        }
    }
    if !run.logs.is_empty() {
        println!("== 动作日志 ==");
        for line in &run.logs {
            println!("  {line}");
        }
    }
    if let Some(reason) = &run.rejected {
        println!("==> 拒绝执行: {reason}");
    }
    println!("== 最终事实 ==");
    println!("{}", serde_json::to_string_pretty(&facts).unwrap());

    ExitCode::SUCCESS
}
