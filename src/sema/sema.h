/*
 * 语义分析: 符号表 + 类型检查
 */

#ifndef C1C_SEMA_H
#define C1C_SEMA_H

#include "ast.h"

typedef enum {
    SYM_VAR, SYM_FUN, SYM_PARAM
} SymKind;

typedef struct Type {
    enum { TY_INT, TY_VOID, TY_INT_ARRAY, TY_FUNC } kind;
    int array_size;        /* TY_INT_ARRAY */
    struct Type *ret;      /* TY_FUNC */
    struct Type **params;  /* TY_FUNC */
    int n_params;
} Type;

typedef struct Symbol {
    char    *name;
    SymKind  kind;
    Type    *type;
    int      line;
    struct Symbol *next;   /* 同层链表 */
} Symbol;

typedef struct Scope {
    Symbol *head;
    struct Scope *parent;
    int level;
} Scope;

typedef struct {
    Scope *current;
    int    n_errors;
    int    n_warnings;
    /* 当前函数 — 用于 return 检查 */
    ASTNode *current_fun;
} SemaContext;

SemaContext *sema_new(void);
void sema_free(SemaContext *ctx);
void sema_check(SemaContext *ctx, ASTNode *program);

void sema_enter_scope(SemaContext *ctx);
void sema_leave_scope(SemaContext *ctx);
Symbol *sema_declare(SemaContext *ctx, const char *name, SymKind k, Type *t, int line);
Symbol *sema_lookup(SemaContext *ctx, const char *name);

/* 类型工具 */
Type *type_int(void);
Type *type_void(void);
Type *type_int_array(int n);
Type *type_func(Type *ret, Type **params, int n);
int   type_equal(Type *a, Type *b);
const char *type_str(Type *t);
void sema_error(SemaContext *ctx, int line, const char *fmt, ...);

#endif
