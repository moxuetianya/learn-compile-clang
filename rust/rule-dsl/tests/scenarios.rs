//! 端到端集成测试：完整跑 词法 → 语法 → 执行，验证各场景的行为。
//! 对照教程主线的 e2e 测试（tests/e2e/），思路一样：给定输入，断言最终效果。

use rule_dsl::{engine, error::RuntimeError, parser, token};
use serde_json::{Value, json};

/// 词法 → 语法 → 执行一条龙；成功返回 (执行报告, 修改后的事实)
fn run_rules(rules: &str, facts: Value) -> Result<(engine::RunReport, Value), RuntimeError> {
    let tokens = token::lex(rules).expect("lex 不应失败");
    let eoi = tokens.last().map(|(_, s)| s.end..s.end).unwrap_or(0..0);
    let (ast, errs) = parser::parse(tokens, eoi);
    assert!(errs.is_empty(), "语法不应出错: {errs:?}");
    let program = ast.expect("应解析出 AST");
    let mut facts = facts;
    let report = engine::run(&program, &mut facts)?;
    Ok((report, facts))
}

#[test]
fn vip_big_discount_scenario() {
    let (run, facts) = run_rules(
        include_str!("../rules/orders.rules"),
        json!({
            "user": { "name": "Alice", "vip": true, "blacklisted": false },
            "order": { "id": "A-1001", "amount": 1580.0, "items": 6, "discount": 0 }
        }),
    )
    .unwrap();

    assert_eq!(run.fired, ["VIP 大额订单"]);
    assert!(run.rejected.is_none());
    // set 的效果写回了 facts：折扣 0.15、打了标签
    assert_eq!(facts["order"]["discount"], json!(0.15));
    assert_eq!(facts["order"]["tag"], json!("vip-big"));
    assert!(run.logs.iter().any(|l| l.contains("vip")));
}

#[test]
fn blacklisted_user_is_rejected() {
    let (run, _) = run_rules(
        include_str!("../rules/orders.rules"),
        json!({
            "user": { "name": "M", "vip": true, "blacklisted": true },
            "order": { "id": "D-1", "amount": 5000.0, "items": 10, "discount": 0 }
        }),
    )
    .unwrap();
    assert_eq!(run.rejected.as_deref(), Some("user is blacklisted"));
    // reject 之后不应再执行其他规则
    assert_eq!(run.fired.len(), 1);
}

#[test]
fn normal_order_gets_standard_discount_and_shipping() {
    let (run, facts) = run_rules(
        include_str!("../rules/orders.rules"),
        json!({
            "user": { "name": "Bob", "vip": false, "blacklisted": false },
            "order": { "id": "B-1", "amount": 320.0, "items": 2, "discount": 0 }
        }),
    )
    .unwrap();
    // 按优先级：先满减（5），后运费（1）；满减 set 的 discount 影响后续规则可见
    assert_eq!(run.fired, ["普通满减", "标准运费"]);
    assert_eq!(facts["order"]["discount"], json!(0.05));
    assert_eq!(facts["order"]["shipping"], json!(3.2));
}

#[test]
fn small_order_free_shipping() {
    let (run, facts) = run_rules(
        include_str!("../rules/orders.rules"),
        json!({
            "user": { "name": "Carol", "vip": false, "blacklisted": false },
            "order": { "id": "C-1", "amount": 30.0, "items": 1, "discount": 0 }
        }),
    )
    .unwrap();
    assert_eq!(run.fired, ["小额订单免运费"]);
    assert_eq!(facts["order"]["shipping"], json!(0));
}

#[test]
fn runtime_error_on_missing_path() {
    let rules = r#"
        rule "引用不存在的字段" when order.nonexistent > 1 then end
    "#;
    let tokens = token::lex(rules).unwrap();
    let (ast, _) = parser::parse(tokens, 0..0);
    let mut facts = json!({ "order": { "amount": 1 } });
    let err = engine::run(&ast.unwrap(), &mut facts).unwrap_err();
    // 错误信息里应包含路径名，span 指向源码中的表达式（ariadne 定位用）
    assert!(err.to_string().contains("order.nonexistent"));
    assert!(!err.span().is_empty());
}

#[test]
fn set_creates_intermediate_objects() {
    let (run, facts) = run_rules(
        r#"
            rule "补全默认值" when true then
                set audit.checked = true;
            end
        "#,
        json!({ "order": { "amount": 1 } }),
    )
    .unwrap();
    assert_eq!(run.fired.len(), 1);
    assert_eq!(facts["audit"]["checked"], json!(true));
}

#[test]
fn int_division_and_float_promotion() {
    // 10 / 3 = 3（整数除法），10 / 4.0 = 2.5（提升为浮点）——语义设计见教程 15.6
    let (_, facts) = run_rules(
        r#"
            rule "算术" when true then
                set a = 10 / 3;
                set b = 10 / 4.0;
            end
        "#,
        json!({}),
    )
    .unwrap();
    assert_eq!(facts["a"], json!(3));
    assert_eq!(facts["b"], json!(2.5));
}

#[test]
fn string_concat_and_compare() {
    let (_, facts) = run_rules(
        r#"
            rule "字符串" when user.name + "!" == "Alice!" && "a" < "b" then
                set greeting = "hello " + user.name;
            end
        "#,
        json!({ "user": { "name": "Alice" } }),
    )
    .unwrap();
    assert_eq!(facts["greeting"], json!("hello Alice"));
}

#[test]
fn broken_rules_file_recovers_good_rules() {
    let rules = include_str!("../rules/broken.rules");
    let tokens = token::lex(rules).unwrap();
    let (ast, errs) = parser::parse(tokens, 0..0);
    assert_eq!(errs.len(), 2, "应报告 2 个语法错误");
    let ast = ast.expect("错误恢复应保住好规则");
    let names: Vec<&str> = ast.rules.iter().map(|r| r.name.as_str()).collect();
    assert_eq!(names, ["好规则1", "好规则2", "好规则3"]);
}
