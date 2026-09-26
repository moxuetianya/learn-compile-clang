/*
 * AST 数据结构 + 构造辅助函数
 * 与 src/parser/c1.y 配套使用
 */

#ifndef C1C_AST_H
#define C1C_AST_H

#include <stdio.h>



/* AST 节点类型 */
typedef enum {
    AST_PROGRAM,
    AST_VAR_DECL,
    AST_FUN_DECL,
    AST_PARAM,
    AST_PARAM_LIST,
    AST_COMPOUND,
    AST_EXPR_STMT,
    AST_IF,
    AST_WHILE,
    AST_RETURN,
    AST_ASSIGN,
    AST_BINOP,
    AST_VAR,
    AST_CALL,
    AST_NUM
} ASTKind;

/* 类型（用于变量/函数） */
typedef enum {
    T_INT,
    T_VOID,
    T_INT_ARRAY,
    T_FUNC
} TypeKind;

/* AST 节点 —— 用 tagged union 紧凑表示 */
typedef struct ASTNode {
    ASTKind  kind;
    int      line;          /* 出错信息用 */
    TypeKind type;          /* 类型检查后填 */
    union {
        struct {
            struct ASTNode **decls;
            int n;
        } program;

        struct {
            char    *name;
            TypeKind type;
            int      array_size;
        } var_decl;

        struct {
            char        *name;
            TypeKind     ret_type;
            struct ASTNode **params;
            int          n_params;
            struct ASTNode *body;
            void        *ret_type_obj;   /* 语义分析后填充的 Type* (前向声明避免循环引用) */
        } fun_decl;

        struct {
            char    *name;
            TypeKind type;
            int      is_array;
        } param;

        struct {
            struct ASTNode **items;
            int n;
        } param_list;

        struct {
            struct ASTNode **decls;
            int nd;
            struct ASTNode **stmts;
            int ns;
        } compound;

        struct {
            struct ASTNode *expr;
        } expr_stmt;

        struct {
            struct ASTNode *cond, *then_br, *else_br;
        } if_stmt;

        struct {
            struct ASTNode *cond, *body;
        } while_stmt;

        struct {
            struct ASTNode *expr;
        } ret_stmt;

        struct {
            struct ASTNode *var, *expr;
        } assign;

        struct {
            int op;                   /* token type, e.g. PLUS, MINUS, EQ ... */
            struct ASTNode *lhs, *rhs;
        } binop;

        struct {
            char *name;
            struct ASTNode *index;
        } var;

        struct {
            char *name;
            struct ASTNode **args;
            int n;
        } call;

        struct {
            int value;
        } num;
    } u;
} ASTNode;

/* 构造器 */
ASTNode *mk_decl_list(int n, ASTNode *first);
ASTNode *append_decl(ASTNode *list, ASTNode *d);

ASTNode *mk_var_decl(char *name, TypeKind t, int array_size, int line);
ASTNode *mk_fun_decl(char *name, TypeKind ret, ASTNode *params, ASTNode *body, int line);

ASTNode *mk_param(char *name, TypeKind t, int is_array, int line);
ASTNode *mk_param_list(ASTNode *first);
ASTNode *append_param(ASTNode *list, ASTNode *p);

ASTNode *mk_compound(ASTNode *decls, ASTNode *stmts, int line);
ASTNode *append_local(ASTNode *list, ASTNode *d);
ASTNode *append_stmt(ASTNode *list, ASTNode *s);

ASTNode *mk_expr_stmt(ASTNode *e, int line);
ASTNode *mk_if(ASTNode *cond, ASTNode *then_br, ASTNode *else_br, int line);
ASTNode *mk_while(ASTNode *cond, ASTNode *body, int line);
ASTNode *mk_ret(ASTNode *e, int line);

ASTNode *mk_assign(ASTNode *var, ASTNode *expr, int line);
ASTNode *mk_binop(int op, ASTNode *lhs, ASTNode *rhs, int line);
ASTNode *mk_var(char *name, ASTNode *idx, int line);
ASTNode *mk_call(char *name, ASTNode *args, int line);
ASTNode *mk_num(int value, int line);

ASTNode *mk_arg_list(ASTNode *first);
ASTNode *append_arg(ASTNode *list, ASTNode *e);

/* 工具 */
void ast_free(ASTNode *n);
void ast_print(ASTNode *n, int indent);
void ast_to_dot(ASTNode *n, FILE *out);

const char *kind_name(ASTKind k);
const char *type_name(TypeKind t);

#endif
