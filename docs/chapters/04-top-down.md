# 第 04 章：自顶向下 —— 递归下降与 LL(1)

> 上一章讲了理论，本章把它落到代码上。我们要写一个 **手写的递归下降分析器**，理解为什么说"递归下降是写起来最舒服的语法分析器"。

## 4.1 递归下降分析器的形态

每个非终结符 = 一个函数：

```
parseProgram() { parseDeclList(); }
parseDeclList() {
    while (lookahead 在 FIRST(decl)) parseDecl();
}
parseDecl() {
    if (lookahead == INT) parseVarDecl();
    else if (lookahead == VOID) parseFunDecl();
    else error();
}
parseVarDecl() { /* 匹配 type-specifier ID [NUM] ; */ }
...
```

**三个关键动作**：
1. `match(token)` —— 当前 lookahead 必须是 `token`，否则报错；匹配后读下一个
2. `consume()` —— 取下一个 token
3. `expect(token)` —— 等价于 match，但允许 peek

## 4.2 完整示例：算术表达式

文法（已经消过左递归、提取过左公因子）：

```
expr   → term expr'
expr'  → '+' term expr' | '-' term expr' | ε
term   → factor term'
term'  → '*' factor term' | '/' factor term' | ε
factor → '(' expr ')' | NUM
```

C 代码（节选）：

```c
typedef enum { T_PLUS, T_MINUS, T_MUL, T_DIV, T_LPAREN, T_RPAREN, T_NUM, T_EOF } Tok;
static Tok cur;
static int  cur_val;       /* 当前 NUM 的值 */

void advance(void) { cur = yylex(); cur_val = yylval.num; }

void match(Tok t) {
    if (cur != t) { fprintf(stderr, "expected %d got %d\n", t, cur); exit(1); }
    advance();
}

/* expr → term expr' */
int parse_expr(void) {
    int v = parse_term();
    return parse_expr_tail(v);
}

/* expr' → ('+'|'-') term expr' | ε */
int parse_expr_tail(int lhs) {
    if (cur == T_PLUS) { advance(); int rhs = parse_term(); return parse_expr_tail(lhs + rhs); }
    if (cur == T_MINUS){ advance(); int rhs = parse_term(); return parse_expr_tail(lhs - rhs); }
    return lhs;  /* ε */
}

int parse_term(void) {
    int v = parse_factor();
    return parse_term_tail(v);
}
int parse_term_tail(int lhs) {
    if (cur == T_MUL){ advance(); int rhs = parse_factor(); return parse_term_tail(lhs * rhs); }
    if (cur == T_DIV){ advance(); int rhs = parse_factor(); return parse_term_tail(lhs / rhs); }
    return lhs;
}
int parse_factor(void) {
    if (cur == T_LPAREN) { advance(); int v = parse_expr(); match(T_RPAREN); return v; }
    if (cur == T_NUM)    { int v = cur_val; advance(); return v; }
    error();
}
```

**特点**：
- 几乎就是把 BNF 一行行翻成函数
- 没用任何工具
- 顺手把 **求值** 做了（既分析又计算）

## 4.3 这段代码的局限

| 问题 | 后果 |
| ---- | ---- |
| 没有左递归处理 | 文法一改就死循环 |
| 错误恢复弱 | 第一个错就退出 |
| 错误信息不精确 | 不知道哪儿出问题 |
| 文法必须 LL(1) | C1 大部分不满足 |

所以：**递归下降适合小语言、玩具项目、教学**；工业级还是要 LR。

## 4.4 扩展：手写一个 C1 子集

我们真正要做的（也是 Clang 的方式）：

```
parseTranslationUnit()       // 文件顶层
parseExternalDeclaration()   // 顶层声明
parseFunctionDefinition()    // 函数定义
parseDeclSpecifiers()        // int/void
parseDeclarator()            // ID [NUM] 或 ID (params)
parseCompoundStatement()     // { ... }
parseStatement()             // if/while/return/expression
parseExpression()            // 优先级递归下降
parseAssignmentExpression()
parseAdditiveExpression()
parseMultiplicativeExpression()
...
```

**每个优先级一层函数**，正好和"表达式优先级"对应。

## 4.5 预测分析表（表驱动 LL(1)）

另一种实现方式：先用第 3 章的方法算出预测分析表 `M[非终结符, 终结符]`，再用栈驱动。

代码骨架：

```c
stack = [$, S];     /* 起始符号 */
while (top(stack) != $) {
    X = pop(stack);
    if (is_terminal(X)) {
        if (X == cur) advance();
        else error();
    } else {
        prod = M[X, cur];
        if (!prod) error();
        push_reverse(prod.rhs, stack);
    }
}
```

这种"表驱动" + "递归下降"得到的结果相同，只是实现形态不同。表驱动实现起来繁琐，不如递归下降直观，**教学价值大于实用价值**。

## 4.6 错误恢复

递归下降的错误恢复策略：

1. **同步集合**：每个非终结符有"同步 token 集"，出错时跳到这些 token
2. **跳过 token**：在错误处不断 `advance()`，直到遇到 FOLLOW 或同步集中的 token
3. **插入/删除**：某些教学系统会"假设用户漏写了分号"，自动补一个再继续

实际编译器倾向于**保守报告**：宁可少报，不要瞎猜。

## 4.7 小结

- 递归下降 = 文法 → 函数 的直接翻译
- 适合：小型语言、教学、文法简单的 DSL
- 不适合：C1 这种有左递归 + 复杂优先级的语言
- 工业界混用：手写 RD + flex/bison（bison 干重活儿）

**思考题**：
1. 把上面的 `parse_expr` 改成同时构建 AST（不再返回 int，而是返回 AST 节点指针）。
2. 给 `parse_statement` 写一个同步集合（比如 `{ ';', '}', KW_INT, KW_RETURN }`）。
3. 为什么"递归下降函数没有返回值"也能工作（用全局变量 `cur`）？这样做有什么坏处？

---
**下一章**：[第 05 章 自底向上：LR](05-bottom-up.md) —— 包含本教程的核心难点。
