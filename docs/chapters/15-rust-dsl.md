# 第 15 章 专题：用 Rust 实现外部 DSL —— 一个规则引擎

> 前面 14 章的主线是 C/C++：flex/bison 做前端，LLVM 做后端。
> 本章换一门语言、换一种目标：用 **Rust** 实现一个**外部 DSL**（领域专用语言），
> 场景选最常见的 **规则引擎**——风控、促销、审批，本质都是"一堆 if-then"。
>
> 你会发现：**编译原理的知识完全可迁移**，变的只是工具。词法还是最长匹配，
> 语法还是优先级分层，错误恢复还是 bison 的 `error` 记号那一套思路。

## 15.1 为什么用 DSL、为什么用 Rust

### 什么是外部 DSL

| 类型 | 定义 | 例子 | 实现方式 |
| ---- | ---- | ---- | -------- |
| 内部 DSL | 宿主语言语法的一个子集 | Rust 的 axum 路由、sqlx 宏 | proc macro，不写词法/语法分析 |
| **外部 DSL** | 独立的小语言 | SQL、正则、Makefile、TOML | **自己实现 词法→语法→执行** |

什么时候值得做一个外部 DSL？规则会**频繁变更**、要**交给非程序员维护**、
或者要**独立于主应用部署**的时候——把规则硬编码在 Rust/C 里就输了。

### 规则引擎的需求原型

```text
输入：facts（一段 JSON，描述"当前发生了什么"）
规则：一堆 when <条件> then <动作>
输出：修改后的 facts + 执行轨迹（命中了哪些规则、是否拒绝）
```

### 工具链选型（对照主线）

| 阶段 | C/C++ 主线 | 本章（Rust） | 说明 |
| ---- | ---------- | ------------ | ---- |
| 词法 | flex（生成 C 代码） | **logos** | 过程宏在编译期生成 DFA，无独立生成步骤 |
| 语法 | bison（LR(1) 表格） | **chumsky** | 递归下降组合子库，文法即 Rust 代码 |
| 语义/执行 | LLVM IR → 机器码 | **树遍历求值** | DSL 规模小，直接遍历 AST 解释执行 |
| 错误报告 | `fprintf(stderr, "line %d")` | **ariadne** | 画出错的那行源码 + 下划线定位 |

> 类似生态位的其它选择：词法还有 `nom`/手写；语法还有 `lalrpop`（LR 生成器，
> 更像 bison）、`pest`（PEG）；本教程选 logos + chumsky，因为它们与
> flex + bison 的"正则 → 自动机 / 文法 → 分析器"心智模型对应得最整齐，
> 且 chumsky 的**错误恢复**是 DSL 开发的刚需。

## 15.2 DSL 设计：先写文法，再写代码

和写 C1 编译器一样，动手前先把文法定下来（这就是第 3 章的 CFG）：

```text
program := rule* EOF
rule    := "rule" STRING ("priority" INT)? "when" expr "then" action* "end"
action  := "set" path "=" expr ";"
         | "log" "(" expr ("," expr)* ")" ";"
         | "reject" "(" expr ")" ";"
expr    := or
or      := and  ("||" and)*              ← 优先级最低
and     := cmp  ("&&" cmp)*
cmp     := add  (("=="|"!="|">"|">="|"<"|"<=") add)?
add     := mul  (("+"|"-") mul)*
mul     := unary (("*"|"/"|"%") unary)*
unary   := ("!"|"-") unary | atom        ← 优先级最高
atom    := INT | FLOAT | STRING | "true" | "false" | path | "(" expr ")"
path    := IDENT ("." IDENT)*
```

一段实际的规则（`rules/orders.rules`）：

```text
rule "VIP 大额订单" priority 10
when order.amount > 1000 && user.vip == true && order.items >= 5
then
    set order.discount = 0.15;
    set order.tag = "vip-big";
    log("vip 大额折扣生效，订单金额=", order.amount);
end
```

**语义设计决策**（每一条都是设计，不是默认答案）：

1. **facts 就是 JSON**（`serde_json::Value`）——不发明新数据模型，
   `order.amount` 直接下钻 JSON 对象，和真实规则引擎一致；
2. 规则按 **priority 从大到小**执行，同优先级按书写顺序；
3. **所有命中的规则都执行**（不是首中即停），`reject` 才终止；
   前面规则 `set` 的值，后面的规则**看得见**——所以规则有顺序依赖；
4. 引用不存在的事实路径是**运行期错误**（宁可报错也不悄悄当 false，
   帮用户抓拼写错误）；
5. 整数 `/` 整数 = 整数（向零截断），任一操作数是浮点就提升为浮点。

## 15.3 架构：五步流水线

```text
          ┌────────┐   ┌──────────┐   ┌─────────┐   ┌────────┐   ┌────────┐
 .rules ─→│ token  │ ─→│  parser  │ ─→│   AST   │ ─→│ engine │ ─→│ 修改后 │
  源码    │ logos  │   │ chumsky  │   │ (带Span)│   │ 树遍历  │   │ facts  │
          └────────┘   └──────────┘   └─────────┘   └────────┘   └────────┘
           ≈ flex       ≈ bison          ≈ src/ast    ≈ sema+解释   （+ JSON 输出）
                                        （第 7 章）    （第 8/13 章）
                    错误（带字节偏移的 Span）贯穿始终 → report.rs → ariadne
```

代码在 [`rust/rule-dsl/`](../../rust/rule-dsl/)，每个阶段一个文件：

| 文件 | 内容 |
| ---- | ---- |
| `src/token.rs` | Token 定义 + logos 用法 + `unquote` |
| `src/parser.rs` | 组合子文法 + 优先级 + 错误恢复 |
| `src/ast.rs` | AST（Span 存进节点） |
| `src/engine.rs` | 求值、调度、facts 读写 |
| `src/report.rs` | ariadne 渲染 |
| `src/main.rs` | CLI 驱动 |
| `tests/scenarios.rs` | 9 个端到端场景 |

## 15.4 词法：logos ≈ 声明式的 flex

把 Token 声明出来，logos 的过程宏在**编译期**生成 DFA（flex 是提前跑一个生成器）：

```rust
#[derive(Logos, Debug, Clone, PartialEq)]
#[logos(skip r"[ \t\r\n\f]+")]   // 空白，≈ flex 的 [ \t\n] ;
#[logos(skip r"#.*")]            // 注释，≈ flex 的 "#".*
pub enum Token {
    #[token("rule")]
    Rule,                                    // ≈ "rule"   { return RULE; }
    #[regex(r"[0-9]+", |lex| lex.slice().parse::<i64>().ok())]
    Int(i64),                                // ≈ [0-9]+   { yylval = ...; return INT; }
    #[regex(r"[a-zA-Z_][a-zA-Z0-9_]*", |lex| lex.slice().to_string())]
    Ident(String),                           // ≈ [a-zA-Z_][a-zA-Z0-9_]* { return IDENT; }
    #[token("==")]
    EqEq,
    // ... 其余 token
}
```

要点（对照第 1、2 章的概念）：

- **最长匹配**：`3.14` 匹配 Float 而不是两个 Int 夹一个 Dot——lex 时代的规则原样适用；
- **等长优先级**：字面量 token（关键字/运算符）胜过 `Ident` 正则，
  所以 `rule` 是 `Token::Rule` 不是标识符；
- **回调里直接构造 payload**：`parse::<i64>()` 返回 `None` 时该 token 成为词法错误
  （整数溢出会优雅报错而不是 panic）；
- `lex()` 产出 `Vec<(Token, Span)>`——**Span 从第一站就跟着走**，
  这是后面所有错误定位的基础。

## 15.5 语法：chumsky 的组合子文法

### 优先级分层：和手写递归下降一模一样

第 4 章手写递归下降时，我们为每个优先级写一个函数（`parseExpr → parseTerm → parseFactor`）。
chumsky 只是把"函数"换成了"值"——每层产生式是一个 parser 值：

```rust
// binary_level：operand (op operand)* ，左结合
fn binary_level(operand, ops) -> impl Parser<...> {
    operand
        .clone()
        .then(ops.then(operand).repeated().collect::<Vec<_>>())
        .map_with(|(first, rest), _| {
            rest.into_iter().fold(first, |(lhs, lspan), (op, (rhs, rspan))| {
                let span = lspan.start..rspan.end;          // 折叠时顺手算出精确 span
                (Expr::Binary(op, Box::new(lhs), Box::new(rhs), span.clone()), span)
            })
        })
}

// 从高到低逐层搭（对照 15.2 的文法）
let mul = binary_level(unary, choice((Star→Mul, Slash→Div, Percent→Rem)));
let add = binary_level(mul,   choice((Plus→Add, Minus→Sub)));
let cmp = binary_level(add,   choice((EqEq→Eq, NotEq→Ne, /* ... */)));
let and = binary_level(cmp,   just(AndAnd).to(BinOp::And));
let or  = binary_level(and,   just(OrOr).to(BinOp::Or));   // or 就是 expr
```

与 bison 的 `%left`/`%prec` 声明相比：**优先级体现在分层的函数结构里**，
左结合体现在 `fold` 上——更啰嗦，但每一行都能单步调试。

### 错误恢复：bison `error` 记号的现代版本

第 6 章讲过 bison 的 `error` 记号：出错后丢弃符号直到能继续。chumsky 对应物：

```rust
// 一条规则写坏 → 跳过若干 token 逐位重试（通常在下一个 rule 关键字处成功）
rule_parser(...)
    .recover_with(skip_then_retry_until(any().ignored(), end()))
```

效果（`rules/broken.rules`，两处语法错误，中间夹着好规则）：

```text
[E0002] Error: 语法错误：期望 `整数`，实际找到 when
   ╭─[ rules/broken.rules:7:24 ]
   │
 7 │ rule "缺优先级数字" priority when true then end
   │                              ──┬─
   │                                ╰─── 这里不对
───╯
[E0002] Error: 语法错误：期望 `*` 或 `/` 或 `%` 或 `+` 或 `-` 或 `==` 或 ... 实际找到 ;
    ╭─[ rules/broken.rules:14:13 ]
    │
 14 │     log("测试";
    │               ┬
    │               ╰── 这里不对
────╯
（2 个语法错误；错误恢复保住了 3 条规则，继续执行）
== 规则命中（按优先级执行顺序） ==
  ✓ 好规则1
  ✓ 好规则2
  ✓ 好规则3
```

一次报出**全部**错误、坏规则不拖垮好规则——这就是恢复的意义（IDE 的体验基线）。

`labelled("整数")` 值得一提：`select!` 失败时默认只说"期望某种特定 token"，
贴上标签后错误信息变成人话。

### 踩坑实录（值得记笔记的三个）

1. **`repeated()`/`separated_by()` 返回"迭代式 parser"**，必须接 `.collect()`
   才是普通 parser——第一次写几乎必踩；
2. 泛型上下文里 `map_with` 的**类型推断很弱**：用到 `e.span()` 的组合子
   都收进签名完整的独立函数里（本项目的 `spanned`/`path_expr`/`unary_expr` 就是这么来的）；
3. chumsky 1.0 长期处于 alpha，本教程用的 0.10 稳定版与 1.0-alpha 的 API
   差异不小，查资料时注意版本。

## 15.6 执行：树遍历求值（对照第 8、13 章）

没有 codegen，`eval` 就是一个对 `Expr` 的大 `match`——这正是第 13 章
"解释执行"的形态，只是 IR 换成了 AST 本身：

```rust
fn eval(e: &Expr, facts: &Value) -> Result<Value, RuntimeError> {
    match e {
        Expr::Path(segs, span) => lookup_path(facts, segs, span),  // order.amount 下钻 JSON
        Expr::Binary(BinOp::And | BinOp::Or, ..) => { /* 短路求值 */ }
        Expr::Binary(op, l, r, span) => { /* 先 eval 两边再按 op 分派 */ }
        // ...
    }
}
```

**调度**（规则引擎的核心循环）：

```rust
let mut rules: Vec<&Rule> = program.rules.iter().collect();
rules.sort_by(|a, b| b.priority.cmp(&a.priority)      // priority 大的先
                       .then(a.span.start.cmp(&b.span.start))); // 同级按书写顺序（稳定排序）

for rule in rules {
    match eval(&rule.when, facts)? {          // 条件不是布尔 → 运行期错误
        Value::Bool(false) => continue,
        Value::Bool(true) => { /* 执行动作 */ }
        other => return Err(RuntimeError::CondNotBool { .. }),
    }
}
```

几个体现"语义设计"的点：

- **`&&`/`||` 短路**：`(a > b) && (heavy_check)`——右边可能引用不存在的路径，
  短路让它只在必要时求值；
- **`set` 自动创建中间对象**：`set audit.checked = true` 在 facts 没有 `audit`
  字段时会创建它（对账、补默认值的规则常用）；
- **`reject` 即失败终止**：风控场景的"一票否决"；
- **运算符重载**：`+` 对两个字符串是拼接（第 8 章类型检查的"重载解析"迷你版，
  这里在求值期就地做了）。

## 15.7 错误报告：ariadne 与一个经典坑

`report.rs` 把带 Span 的错误交给 [ariadne](https://docs.rs/ariadne) 渲染，
运行期错误同样能精确定位（Span 一直存到了 AST 里）：

```text
[E0003] Error: 未定义的事实路径 `order.nonexistent`（when/set 里引用了事实里不存在的字段）
   ╭─[ rules/runtime-error.rules:4:6 ]
   │
 4 │ when order.nonexistent > 100
   │      ────────┬────────
   │              ╰────────── 这里
───╯
```

**经典坑：字节偏移 vs 字符偏移。** logos 的 Span 是**字节**偏移，ariadne 要
**字符**偏移（其文档明说 character offsets）。源码含中文注释/字符串时两者不一致，
下划线会画到错误的位置（本项目开发时就真实踩了：报错在 7 行，下划线画到 12 行）。
转换只需要两行：

```rust
fn to_char_span(src: &str, byte_span: &Span) -> Span {
    let start = src[..byte_span.start].chars().count();
    let end = start + src[byte_span.start..byte_span.end].chars().count();
    start..end
}
```

> 这不是 Rust 特有的问题：C/C++ 处理 UTF-8 源码时 byte offset 与显示列号的
> 换算（`clang` 的 `SourceLocation` 同样要区分 FilePos/列号）是同一件事。

## 15.8 跑起来

```bash
cd rust/rule-dsl
cargo test                                        # 5 单测 + 9 端到端
cargo run -- rules/orders.rules facts/vip-big.json
```

VIP 场景的输出：

```text
== 规则命中（按优先级执行顺序） ==
  ✓ VIP 大额订单
== 动作日志 ==
  [VIP 大额订单] vip 大额折扣生效，订单金额= 1580.0
== 最终事实 ==
{
  "order": {
    "amount": 1580.0,
    "discount": 0.15,
    "id": "A-1001",
    "items": 6,
    "tag": "vip-big"
  },
  "user": { "blacklisted": false, "name": "Alice", "vip": true }
}
```

其它场景：`facts/blacklisted.json`（reject）、`rules/broken.rules`（语法错误恢复）、
`rules/runtime-error.rules`（运行期错误）、`--dump-ast`（打印 AST，对照第 7 章）。

## 15.9 对照总结：换语言学到了什么

| 概念 | C/C++ 主线的实现 | 本章的实现 | 不变的是什么 |
| ---- | ---------------- | ---------- | ------------ |
| 词法 | flex 正则 → C | logos 属性宏 → DFA | 最长匹配、等长优先级 |
| 语法 | bison LR(1) 表 | chumsky 组合子 | CFG、优先级/结合性、左递归的规避 |
| AST | 手写节点 + Visitor | enum + 递归 match | AST 是前后端的契约；Span 要随身带 |
| 语义 | 符号表 + 类型检查 pass | 求值期就地检查 | 类型规则是设计决策 |
| 错误恢复 | bison `error` 记号 | `skip_then_retry_until` | 丢弃到同步点再续 |
| 执行 | LLVM IR → 机器码 | 树遍历解释 | 求值语义（短路、结合性）必须先定义清楚 |

以及 Rust 生态特有的经验：过程宏生成分析器（编译期自检）、`Result` 让错误
变成值在流水线里流动、enum+match 表达 AST 天然穷尽所有节点。

## 15.10 练习

1. **加一种动作**：`emit("事件名", expr);` —— 把事件写进 `facts["events"]` 数组；
2. **null 安全访问**：`order.coupon?.value`，路径缺失时返回 null 而不是报错
   （改文法 + 改 `lookup_path`）；
3. **首中即停模式**：`--first-match` 参数，命中最高的优先级规则后停止
   （对照 Drools 的 agenda 语义讨论两种模式的适用场景）；
4. **静态检查 pass**：解析后遍历 AST，`reject` 的参数必须是字符串字面量——
   把第 8 章的"类型检查"作为独立 pass 重演一遍；
5. **性能路径**（硬）：用 cranelift 把 `when` 表达式编译成机器码再对每条事实执行，
   对照第 13 章的 JIT 思路。
