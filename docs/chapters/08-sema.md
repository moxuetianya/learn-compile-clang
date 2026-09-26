# 第 08 章：语义分析

> 语法分析器只能告诉你"长得很像程序"。语义分析告诉你"是不是一个有意义的程序"。本章我们要构建符号表、作用域链、做类型检查。

## 8.1 语义分析做什么

| 任务 | 例子 |
| ---- | ---- |
| 符号解析 | `x` 是哪个声明？ |
| 类型检查 | `if (x)` `x` 是 int 还是 void？ |
| 控制流检查 | `return` 路径是否覆盖？ |
| 常量折叠 | `2 + 3` 直接算出 5 |
| (高级) 重载解析 | C++ 的 `f(1)` 调哪个？ |

C1 是 C 的小子集，主要做前 3 项。

## 8.2 符号表

符号 = 名字 → { 类型, 类别, 位置, ... }。

```
x → { kind: 局部变量, type: int, line: 5 }
fib → { kind: 函数, type: int(int), line: 1 }
```

### 链式作用域

```
global:    x, fib
  main:   y, fib_local
    if:   tmp
```

嵌套结构：每个作用域有自己的表，**外层可见、内层屏蔽**。
实现：

```c
typedef struct Symbol {
    char *name;
    SymbolKind kind;     /* VAR, FUN, PARAM */
    Type *type;
    int line;
    struct Symbol *next; /* 同层链表 */
} Symbol;

typedef struct Scope {
    Scope *parent;
    Symbol *head;        /* 本层所有符号 */
    int level;
} Scope;

void scope_push(void) { /* ... */ }
void scope_pop(void)  { /* ... */ }
Symbol* scope_lookup(const char *name);  /* 从内向外找 */
Symbol* scope_lookup_local(const char *name);  /* 只看本层 */
bool scope_declare(Symbol *sym);  /* 失败：重复声明 */
```

### 哈希加速

字符串哈希是热点，线性链表扛不住 C++ 模板库规模。C1 规模小，链表够用；规模大就上哈希表（`std::unordered_map` 或 open addressing 手写）。

## 8.3 作用域时机

```
进入 function:    push_scope
  进入 compound:  push_scope（也可以合并）
    声明 var:     declare
    引用 var:     lookup
  退出 compound:  pop_scope
退出 function:    pop_scope
```

C1 允许函数内嵌函数吗？不允许。但允许复合语句嵌套——每层都要开新作用域，否则 `if (x) { int x; }` 里的 `x` 会污染外层。

## 8.4 类型系统

### 8.4.1 C1 的类型

| 类型 | 表示 |
| ---- | ---- |
| `int` | 基本整型 |
| `void` | 仅用于函数返回类型或无参数 |
| `int[NUM]` | 整型数组 |
| `int(int, int)` | 函数类型 |

```c
typedef enum { TY_INT, TY_VOID, TY_ARRAY, TY_FUNC } TypeKind;

typedef struct Type {
    TypeKind kind;
    struct Type *elem;   /* 数组元素类型 / 函数返回类型 */
    int size;            /* 数组大小 */
    struct Param *params;/* 函数参数 */
    int n_params;
} Type;
```

### 8.4.2 类型等价

C1 用 **结构等价**（同结构即同类型）。
C/C++ 用 **名等价**（typedef 出来的不同名即不同类型）。

```c
bool type_equal(Type *a, Type *b) {
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case TY_INT: case TY_VOID: return true;
    case TY_ARRAY: return type_equal(a->elem, b->elem) && a->size == b->size;
    case TY_FUNC:
        if (!type_equal(a->elem, b->elem)) return false;
        if (a->n_params != b->n_params) return false;
        for (int i=0;i<a->n_params;i++)
            if (!type_equal(a->params[i].type, b->params[i].type)) return false;
        return true;
    }
}
```

## 8.5 类型检查实现

类型检查 = 一个 **类型化的 visitor**：

```c
typedef struct {
    Type *t;       /* 当前表达式类型 */
    bool is_lvalue;
} ExprType;

ExprType sema_check_expr(ASTNode *e);
```

例：`+` 要求两个操作数都是 int，结果 int。

```c
case AST_BINOP: {
    ExprType l = sema_check_expr(e->u.binop.lhs);
    ExprType r = sema_check_expr(e->u.binop.rhs);
    if (l.t->kind != TY_INT || r.t->kind != TY_INT)
        error(e->line, "operands of '%s' must be int", op_name(e->u.binop.op));
    return (ExprType){ .t = int_type, .is_lvalue = false };
}
```

例：函数调用检查

```c
case AST_CALL: {
    Symbol *s = scope_lookup(e->u.call.name);
    if (!s || s->kind != SYM_FUN) error(e->line, "not a function: %s", e->u.call.name);
    Type *ft = s->type;
    if (ft->n_params != e->u.call.n)
        error(e->line, "wrong number of args");
    for (int i=0; i<ft->n_params; i++) {
        ExprType a = sema_check_expr(e->u.call.args[i]);
        if (!type_equal(a.t, ft->params[i].type))
            error(e->line, "arg %d type mismatch", i);
    }
    return (ExprType){ .t = ft->elem };
}
```

## 8.6 一个完整的语义错误清单

| 错误 | 检测位置 |
| ---- | -------- |
| 标识符未声明 | `Var`/`Call` 引用时 |
| 标识符重复声明 | 同一作用域声明时 |
| 类型不匹配 | 二元运算、赋值、函数实参 |
| `void` 用于表达式 | `void` 出现在 `+` 左边等 |
| `break/continue` 不在循环里 | 检查上层 scope 链 |
| 函数返回值与声明不符 | `return` 类型 vs 函数返回类型 |
| main 不存在或签名不对 | 顶层检查 |

## 8.7 控制流检查

C1 要求：
- 程序最后一个声明必须是 `int main(void) { ... }`
- `main` 必须 `return expression;`
- 非 void 函数的所有路径都要 return

简化版：只检查"main 必须 return int"。

更严格版（作业可做）：

```c
bool all_paths_return(ASTNode *stmt) {
    switch (stmt->kind) {
    case AST_RETURN: return true;
    case AST_IF:
        return all_paths_return(stmt->u.if_stmt.then_br) &&
               (!stmt->u.if_stmt.else_br || all_paths_return(stmt->u.if_stmt.else_br));
    case AST_WHILE: return false;  /* while 可能 0 次，不算覆盖 */
    case AST_COMPOUND:
        for (int i=0;i<stmt->u.compound.ns;i++)
            if (all_paths_return(stmt->u.compound.stmts[i])) return true;
        return false;
    default: return false;
    }
}
```

## 8.8 错误恢复与报告

```c
int n_errors = 0;
void error(int line, const char *fmt, ...) {
    fprintf(stderr, "Error at line %d: ", line);
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    n_errors++;
    if (n_errors > 20) { fprintf(stderr, "too many errors\n"); exit(1); }
}
```

策略：
- **继续分析**，不要第一个错就退出（用户希望一次看到所有错）
- 但要限流（30 个就停）
- 错误信息带 **行号、节点、原因、修复建议**

## 8.9 现场常用方法的优劣

| 场景 | 主流 |
| ---- | ---- |
| 通用编译语言 | 一次遍历 + 类型推断 |
| 脚本语言 | 名字解析 + 运行时类型 |
| 数据流 / SSA-based | 类型 = 在 SSA value 上 propagate |
| Effect 系统（Rust） | 在类型系统层面追踪借用/所有权 |

## 8.10 小结

- 符号表 = 名字 → {类别, 类型, 位置}
- 作用域 = 嵌套栈；每进一个块 push，出时 pop
- 类型检查 = 类型化 visitor
- C1 范围：声明/类型/控制流三类检查

**练习**：
1. 实现 `scope_lookup`：从当前 scope 向外逐层查找。
2. 实现 `type_equal`：函数类型比参数列表（顺序敏感）。
3. 给 `if (cond)` 加类型检查：`cond` 必须是 `int`。
4. 写一个测试：故意写 `void x = 1;`，确认错误信息清晰。

---
**下一章**：[第 09 章 中间表示与 LLVM IR](09-ir-llvm.md) —— 教程核心。
