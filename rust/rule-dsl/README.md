# rule-dsl —— 用 Rust 实现的规则引擎外部 DSL

> 教程第 15 章的配套代码，完整演示 外部 DSL 的三件套：
> **词法（logos）→ 语法（chumsky）→ 执行（树遍历求值）**，外加 ariadne 的源码级错误报告。
> 详细的讲解在 [`docs/chapters/15-rust-dsl.md`](../../docs/chapters/15-rust-dsl.md)。

## DSL 长什么样

```text
rule "VIP 大额订单" priority 10
when order.amount > 1000 && user.vip == true && order.items >= 5
then
    set order.discount = 0.15;
    log("vip 大额折扣生效，订单金额=", order.amount);
end
```

- **when**：条件表达式（`&&`/`||`/`!`、比较、算术、字符串拼接、事实路径 `order.amount`）
- **then**：动作。`set 路径 = 表达式;` 写回事实、`log(...);` 打日志、`reject("原因");` 终止执行
- 事实（facts）就是一段 JSON，规则读它、改它，最后输出修改后的 JSON

## 快速开始

```bash
cd rust/rule-dsl

# 场景 1：VIP 大额订单 → 命中折扣规则
cargo run -- rules/orders.rules facts/vip-big.json

# 场景 2：黑名单用户 → reject 终止
cargo run -- rules/orders.rules facts/blacklisted.json

# 场景 3：语法错误 ×2 → 看错误恢复 + ariadne 定位（好规则照常执行）
cargo run -- rules/broken.rules facts/normal.json

# 场景 4：运行期错误 → 引用了不存在的事实路径
cargo run -- rules/runtime-error.rules facts/normal.json

# 只看 AST（对照 c1c 的玩法）
cargo run -- rules/orders.rules facts/vip-big.json --dump-ast
```

## 代码结构

| 文件 | 阶段 | 干什么 | 对照主线教程 |
| ---- | ---- | ------ | ------------ |
| `src/token.rs` | 词法 | logos 声明 Token，过程宏生成 DFA | flex / `src/lexer/` |
| `src/parser.rs` | 语法 | chumsky 组合子 + 优先级分层 + 错误恢复 | bison / `src/parser/` |
| `src/ast.rs` | IR | AST 定义（带 Span） | `src/ast/` |
| `src/engine.rs` | 执行 | 树遍历求值：优先级调度、短路求值、facts 读写 | sema + 解释执行 |
| `src/report.rs` | 诊断 | ariadne 渲染错误（字节偏移→字符偏移的坑也在里面） | — |
| `src/main.rs` | 驱动 | CLI，串起整条流水线 | `src/driver/` |

## 测试

```bash
cargo test          # 5 个语法单测 + 9 个端到端场景
cargo clippy        # 0 警告
```
