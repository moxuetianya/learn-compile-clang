# 第 07 章：抽象语法树 AST

> 语法分析产出的是 **AST**（Abstract Syntax Tree）。它不是解析树 —— 它更紧凑、更贴近语义。后续所有阶段（语义分析、IR 生成）都基于 AST。

## 7.1 解析树 vs AST

解析树（parse tree）：每个 token 都是叶子，结构 1:1 对应产生式。**冗余**。
AST（abstract syntax tree）：省略不必要的中间节点，**抽象**。

```
表达式: 2 + 3 * 4

解析树:
        expr
       /  |  \
     expr + expr
     |       |  \
   term    term * term
     |       |      |
   factor  factor  factor
     |       |      |
     2       3      4

AST:
        (+)
       /   \
      2    (*)
          /   \
         3     4
```

## 7.2 AST 节点设计

C1 子集需要覆盖这些结构：

| 类别 | 节点 | 字段 |
| ---- | ---- | ---- |
| 程序 | `Program` | decls |
| 声明 | `VarDecl` | name, type, array_size |
| | `FunDecl` | name, ret_type, params, body |
| 语句 | `CompoundStmt` | decls, stmts |
| | `IfStmt` | cond, then, else? |
| | `WhileStmt` | cond, body |
| | `ReturnStmt` | expr? |
| | `ExprStmt` | expr |
| 表达式 | `Assign` | var, expr |
| | `BinOp` | op, lhs, rhs |
| | `Var` | name, index? |
| | `Call` | name, args |
| | `Num` | value |

## 7.3 C 实现：用 tagged union

```c
typedef enum {
    AST_PROGRAM, AST_VAR_DECL, AST_FUN_DECL,
    AST_COMPOUND, AST_IF, AST_WHILE, AST_RETURN, AST_EXPR_STMT,
    AST_ASSIGN, AST_BINOP, AST_VAR, AST_CALL, AST_NUM
} ASTKind;

typedef enum { TYPE_INT, TYPE_VOID, TYPE_INT_ARRAY } TypeKind;

typedef struct ASTNode {
    ASTKind kind;
    TypeKind type;       /* 用于类型检查 */
    int line;            /* 错误信息 */
    union {
        struct { struct ASTNode **decls; int n; } program;
        struct { char *name; TypeKind type; int array_size; } var_decl;
        struct { char *name; TypeKind ret_type;
                 struct ASTNode **params; int n_params;
                 struct ASTNode *body; } fun_decl;
        struct { struct ASTNode **decls; int nd;
                 struct ASTNode **stmts; int ns; } compound;
        struct { struct ASTNode *cond, *then_br, *else_br; } if_stmt;
        struct { struct ASTNode *cond, *body; } while_stmt;
        struct { struct ASTNode *expr; } ret_stmt;
        struct { struct ASTNode *expr; } expr_stmt;
        struct { struct ASTNode *var, *expr; } assign;
        struct { int op; struct ASTNode *lhs, *rhs; } binop;
        struct { char *name; struct ASTNode *index; } var;
        struct { char *name; struct ASTNode **args; int n; } call;
        struct { int value; } num;
    } u;
} ASTNode;
```

构造辅助函数（节选）：

```c
ASTNode* mk_var_decl(char *name, TypeKind t, ASTNode *size, int line) {
    ASTNode *n = calloc(1, sizeof(*n));
    n->kind = AST_VAR_DECL;
    n->line = line;
    n->u.var_decl.name = strdup(name);
    n->u.var_decl.type = t;
    n->u.var_decl.array_size = size ? size->u.num.value : 0;
    return n;
}
```

## 7.4 Visitor 模式

遍历 AST 最常见的需求：
- 打印（debug / 教育）
- 类型检查
- 翻译成 IR

把"做什么"和"数据结构"分离：

```c
typedef struct Visitor Visitor;

typedef struct {
    void (*visit_program)(Visitor*, ASTNode*);
    void (*visit_var_decl)(Visitor*, ASTNode*);
    void (*visit_fun_decl)(Visitor*, ASTNode*);
    void (*visit_compound)(Visitor*, ASTNode*);
    void (*visit_if)(Visitor*, ASTNode*);
    void (*visit_while)(Visitor*, ASTNode*);
    void (*visit_return)(Visitor*, ASTNode*);
    void (*visit_expr_stmt)(Visitor*, ASTNode*);
    void (*visit_assign)(Visitor*, ASTNode*);
    void (*visit_binop)(Visitor*, ASTNode*);
    void (*visit_var)(Visitor*, ASTNode*);
    void (*visit_call)(Visitor*, ASTNode*);
    void (*visit_num)(Visitor*, ASTNode*);
} VisitorVTable;

void ast_accept(Visitor *v, ASTNode *n) {
    switch (n->kind) {
    case AST_PROGRAM:   v->vt->visit_program(v, n); break;
    case AST_VAR_DECL:  v->vt->visit_var_decl(v, n); break;
    /* ... */
    }
}
```

打印 visitor：

```c
typedef struct { Visitor base; int indent; } PrintVisitor;

static void print_indent(int n) { for (int i=0;i<n;i++) printf("  "); }

static void pv_program(Visitor *vb, ASTNode *n) {
    PrintVisitor *v = (PrintVisitor*)vb;
    print_indent(v->indent);
    printf("Program\n");
    v->indent += 2;
    for (int i=0;i<n->u.program.n;i++)
        ast_accept(vb, n->u.program.decls[i]);
    v->indent -= 2;
}
```

LLVM 后端的 codegen visitor（预览，第 9 章展开）：

```c
static void cg_binop(Visitor *vb, ASTNode *n) {
    PrintVisitor *v = (PrintVisitor*)vb;   /* 实际是 CodegenVisitor */
    ast_accept(vb, n->u.binop.lhs);
    ast_accept(vb, n->u.binop.rhs);
    /* LLVM IR: %3 = add i32 %1, %2 */
}
```

## 7.5 内存管理

两种风格：

1. **Arena 分配**：所有节点从一个大 arena 分配，编译器结束时统一释放。
   - 简单、快、内存多
   - Clang 用 `BumpPtrAllocator`
2. **手动 free**：每个节点 free，但父子关系让 free 复杂
   - 容易漏 free

我们用 Arena：

```c
typedef struct Arena {
    char *buf;
    size_t cap, used;
    struct Arena *next;
} Arena;

void* arena_alloc(Arena *a, size_t n) {
    if (a->used + n > a->cap) { /* grow */ }
    void *p = a->buf + a->used;
    a->used += n;
    return p;
}
```

## 7.6 可视化：把 AST 变成 Graphviz

```c
void ast_to_dot(ASTNode *n, FILE *out) {
    fprintf(out, "  node%p [label=\"%s\"];\n", (void*)n, kind_name(n->kind));
    /* 边 */
    switch (n->kind) {
    case AST_BINOP:
        ast_to_dot(n->u.binop.lhs, out);
        ast_to_dot(n->u.binop.rhs, out);
        fprintf(out, "  node%p -> node%p;\n", (void*)n, (void*)n->u.binop.lhs);
        fprintf(out, "  node%p -> node%p;\n", (void*)n, (void*)n->u.binop.rhs);
        break;
    /* ... */
    }
}
```

```bash
./build/c1c --dump-ast examples/fib.c1 > ast.dot
dot -Tpng ast.dot -o ast.png
```

教学场景非常有用 —— 学生能"看见"语法树。

## 7.7 常见 AST 设计取舍

| 决策 | 选项 A | 选项 B | 工业现状 |
| ---- | ------ | ------ | -------- |
| 表达二元运算 | `BinOp{op,lhs,rhs}` 节点 | `Add{lhs,rhs}`, `Sub{lhs,rhs}` | A 更紧凑、B 更明确 |
| 变量声明 | 一个 `VarDecl` 节点 | 拆 `DeclSpec` + `Declarator` | B 更精确处理 C++ 复杂声明 |
| 类型信息 | 节点字段 | 独立 Type 节点 | B 适合 C++/Rust；A 适合 C1 |
| 列表节点 | 数组 | `List{head,tail}` 链表 | 数组快、链表省内存 |
| 位置信息 | 每节点带行号 | 节点链到 Token 流 | A 简单；B 适合高保真 |

Clang 的 AST 是 B 的极致 —— 每个节点链回原始 Token，可以恢复任意信息。

## 7.8 小结

- AST 比解析树更抽象、更适合后续阶段
- Tagged union + visitor 是最朴素的实现
- Arena 分配 = 编译期一把梭哈
- 可视化对教学和调试都极有价值

**练习**：
1. 给 AST 节点加上列号字段。
2. 实现一个 visitor，把所有 `Var` 节点收集到一张表里（用于"未声明变量"检查）。
3. 用 Graphviz 把 `examples/fib.c1` 的 AST 画出来。

---
**下一章**：[第 08 章 语义分析](08-sema.md)
