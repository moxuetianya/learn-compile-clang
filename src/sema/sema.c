/*
 * 语义分析实现 —— 符号表 + 类型检查
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "sema.h"

Type *type_int(void) {
    Type *t = calloc(1, sizeof(Type));
    t->kind = TY_INT;
    return t;
}
Type *type_void(void) {
    Type *t = calloc(1, sizeof(Type));
    t->kind = TY_VOID;
    return t;
}
Type *type_int_array(int n) {
    Type *t = calloc(1, sizeof(Type));
    t->kind = TY_INT_ARRAY;
    t->array_size = n;
    return t;
}
Type *type_func(Type *ret, Type **params, int n) {
    Type *t = calloc(1, sizeof(Type));
    t->kind = TY_FUNC;
    t->ret = ret;
    t->params = params;
    t->n_params = n;
    return t;
}

int type_equal(Type *a, Type *b) {
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
    case TY_INT: case TY_VOID: return 1;
    case TY_INT_ARRAY:
        return a->array_size == b->array_size;
    case TY_FUNC: {
        if (!type_equal(a->ret, b->ret)) return 0;
        if (a->n_params != b->n_params) return 0;
        for (int i = 0; i < a->n_params; i++)
            if (!type_equal(a->params[i], b->params[i])) return 0;
        return 1;
    }}
    return 0;
}

const char *type_str(Type *t) {
    static char buf[64];
    if (!t) return "?";
    switch (t->kind) {
    case TY_INT: return "int";
    case TY_VOID: return "void";
    case TY_INT_ARRAY: snprintf(buf, sizeof(buf), "int[%d]", t->array_size); return buf;
    case TY_FUNC: return "function";
    }
    return "?";
}

SemaContext *sema_new(void) {
    SemaContext *ctx = calloc(1, sizeof(SemaContext));
    ctx->current = calloc(1, sizeof(Scope));  /* global scope */
    ctx->current->level = 0;
    return ctx;
}

void sema_enter_scope(SemaContext *ctx) {
    Scope *s = calloc(1, sizeof(Scope));
    s->parent = ctx->current;
    s->level = ctx->current->level + 1;
    ctx->current = s;
}

void sema_leave_scope(SemaContext *ctx) {
    if (!ctx->current || !ctx->current->parent) return;
    /* free symbols in current scope */
    Symbol *s = ctx->current->head;
    while (s) {
        Symbol *nx = s->next;
        free(s->name);
        free(s);
        s = nx;
    }
    Scope *old = ctx->current;
    ctx->current = old->parent;
    free(old);
}

Symbol *sema_declare(SemaContext *ctx, const char *name, SymKind k, Type *t, int line) {
    /* check duplicate in current scope */
    for (Symbol *s = ctx->current->head; s; s = s->next)
        if (!strcmp(s->name, name)) {
            sema_error(ctx, line, "duplicate declaration of '%s'", name);
            return NULL;
        }
    Symbol *s = calloc(1, sizeof(Symbol));
    s->name = strdup(name);
    s->kind = k;
    s->type = t;
    s->line = line;
    s->next = ctx->current->head;
    ctx->current->head = s;
    return s;
}

Symbol *sema_lookup(SemaContext *ctx, const char *name) {
    for (Scope *sc = ctx->current; sc; sc = sc->parent)
        for (Symbol *s = sc->head; s; s = s->next)
            if (!strcmp(s->name, name)) return s;
    return NULL;
}

void sema_error(SemaContext *ctx, int line, const char *fmt, ...) {
    fprintf(stderr, "Semantic error at line %d: ", line);
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    ctx->n_errors++;
}

void sema_free(SemaContext *ctx) {
    /* 弹出所有 scope 直到只剩 global */
    while (ctx->current && ctx->current->parent) sema_leave_scope(ctx);
    /* free global scope */
    if (ctx->current) {
        Symbol *s = ctx->current->head;
        while (s) {
            Symbol *nx = s->next;
            free(s->name);
            free(s);
            s = nx;
        }
        free(ctx->current);
    }
    free(ctx);
}

/* ---------- 主入口 ---------- */

static Type *check_expr(SemaContext *ctx, ASTNode *e);

static Type *check_var(SemaContext *ctx, ASTNode *v) {
    Symbol *s = sema_lookup(ctx, v->u.var.name);
    if (!s) {
        sema_error(ctx, v->line, "use of undeclared identifier '%s'", v->u.var.name);
        return type_int();
    }
    if (v->u.var.index) {
        if (s->type->kind != TY_INT_ARRAY) {
            sema_error(ctx, v->line, "'%s' is not an array", v->u.var.name);
            return type_int();
        }
        Type *idx_t = check_expr(ctx, v->u.var.index);
        if (idx_t->kind != TY_INT)
            sema_error(ctx, v->line, "array index must be int, got %s", type_str(idx_t));
        return type_int();
    }
    return s->type;
}

static Type *check_call(SemaContext *ctx, ASTNode *c) {
    Symbol *s = sema_lookup(ctx, c->u.call.name);
    if (!s || s->kind != SYM_FUN) {
        sema_error(ctx, c->line, "call to undefined function '%s'", c->u.call.name);
        return type_int();
    }
    if (s->type->kind != TY_FUNC) {
        sema_error(ctx, c->line, "'%s' is not a function", c->u.call.name);
        return type_int();
    }
    if (s->type->n_params != c->u.call.n) {
        sema_error(ctx, c->line, "function '%s' expects %d args, got %d",
                   c->u.call.name, s->type->n_params, c->u.call.n);
    } else {
        for (int i = 0; i < c->u.call.n; i++) {
            Type *at = check_expr(ctx, c->u.call.args[i]);
            Type *pt = s->type->params[i];
            /* 数组参数: int[] 兼容 int[N] (引用传递) */
            if (pt->kind == TY_INT_ARRAY && at->kind == TY_INT_ARRAY) {
                /* ok */
            } else if (!type_equal(at, pt))
                sema_error(ctx, c->line,
                    "arg %d of '%s': expected %s, got %s",
                    i, c->u.call.name, type_str(pt), type_str(at));
        }
    }
    return s->type->ret;
}

static Type *check_expr(SemaContext *ctx, ASTNode *e) {
    if (!e) return type_void();
    switch (e->kind) {
    case AST_NUM: return type_int();
    case AST_VAR: return check_var(ctx, e);
    case AST_CALL: return check_call(ctx, e);
    case AST_ASSIGN: {
        Type *vt = check_var(ctx, e->u.assign.var);
        Type *et = check_expr(ctx, e->u.assign.expr);
        if (vt->kind != TY_INT && vt->kind != TY_INT_ARRAY) {
            sema_error(ctx, e->line, "cannot assign to non-lvalue");
        }
        if (!type_equal(vt, et))
            sema_error(ctx, e->line, "assignment type mismatch: %s vs %s",
                       type_str(vt), type_str(et));
        return vt;
    }
    case AST_BINOP: {
        Type *lt = check_expr(ctx, e->u.binop.lhs);
        Type *rt = check_expr(ctx, e->u.binop.rhs);
        /* 关系运算返回 int (boolean), 算术运算要求两个 int */
        if (lt->kind != TY_INT || rt->kind != TY_INT) {
            /* unary minus style: 0 - x */
            /* 比较运算是合法的 if 条件 */
        }
        /* 简化: 全部当 int 操作 */
        if (lt->kind != TY_INT)
            sema_error(ctx, e->line, "left operand of binary op must be int, got %s", type_str(lt));
        if (rt->kind != TY_INT)
            sema_error(ctx, e->line, "right operand of binary op must be int, got %s", type_str(rt));
        /* 关系运算返回 int, 算术运算返回 int */
        return type_int();
    }
    default: return type_int();
    }
}

static void check_stmt(SemaContext *ctx, ASTNode *s);

static void check_compound(SemaContext *ctx, ASTNode *c) {
    if (!c) return;
    sema_enter_scope(ctx);
    for (int i = 0; i < c->u.compound.nd; i++) {
        ASTNode *v = c->u.compound.decls[i];
        if (v->kind == AST_VAR_DECL) {
            Type *t = v->u.var_decl.array_size ? type_int_array(v->u.var_decl.array_size) : type_int();
            sema_declare(ctx, v->u.var_decl.name, SYM_VAR, t, v->line);
        }
    }
    for (int i = 0; i < c->u.compound.ns; i++)
        check_stmt(ctx, c->u.compound.stmts[i]);
    sema_leave_scope(ctx);
}

static void check_stmt(SemaContext *ctx, ASTNode *s) {
    if (!s) return;
    switch (s->kind) {
    case AST_COMPOUND:
        check_compound(ctx, s);
        break;
    case AST_EXPR_STMT:
        if (s->u.expr_stmt.expr) check_expr(ctx, s->u.expr_stmt.expr);
        break;
    case AST_RETURN: {
        ASTNode *fun = ctx->current_fun;
        Type *expected = (fun && fun->kind == AST_FUN_DECL) ? fun->u.fun_decl.ret_type_obj : type_void();
        if (s->u.ret_stmt.expr) {
            Type *et = check_expr(ctx, s->u.ret_stmt.expr);
            if (!type_equal(et, expected))
                sema_error(ctx, s->line, "return type mismatch: expected %s, got %s",
                           type_str(expected), type_str(et));
        } else {
            if (expected->kind != TY_VOID)
                sema_error(ctx, s->line, "non-void function must return a value");
        }
        break;
    }
    case AST_IF:
        check_expr(ctx, s->u.if_stmt.cond);
        check_stmt(ctx, s->u.if_stmt.then_br);
        check_stmt(ctx, s->u.if_stmt.else_br);
        break;
    case AST_WHILE:
        check_expr(ctx, s->u.while_stmt.cond);
        check_stmt(ctx, s->u.while_stmt.body);
        break;
    default: break;
    }
}

void sema_check(SemaContext *ctx, ASTNode *program) {
    if (!program || program->kind != AST_PROGRAM) return;

    /* 第一遍: 收集所有函数声明 (forward declare) */
    for (int i = 0; i < program->u.program.n; i++) {
        ASTNode *d = program->u.program.decls[i];
        if (d->kind != AST_FUN_DECL) continue;
        Type **params = NULL;
        if (d->u.fun_decl.n_params > 0) {
            params = calloc(d->u.fun_decl.n_params, sizeof(Type*));
            for (int j = 0; j < d->u.fun_decl.n_params; j++) {
                ASTNode *p = d->u.fun_decl.params[j];
                params[j] = p->u.param.is_array ? type_int_array(0) : type_int();
            }
        }
        Type *ret = (d->u.fun_decl.ret_type == T_VOID) ? type_void() : type_int();
        Type *ft = type_func(ret, params, d->u.fun_decl.n_params);
        sema_declare(ctx, d->u.fun_decl.name, SYM_FUN, ft, d->line);
        /* 保存 ret_type_obj 引用以便后面使用 */
        d->u.fun_decl.ret_type_obj = ret;
    }

    /* 第二遍: 全局变量声明 */
    for (int i = 0; i < program->u.program.n; i++) {
        ASTNode *d = program->u.program.decls[i];
        if (d->kind != AST_VAR_DECL) continue;
        Type *t = d->u.var_decl.array_size ? type_int_array(d->u.var_decl.array_size) : type_int();
        sema_declare(ctx, d->u.var_decl.name, SYM_VAR, t, d->line);
    }

    /* 第三遍: 函数体 */
    for (int i = 0; i < program->u.program.n; i++) {
        ASTNode *d = program->u.program.decls[i];
        if (d->kind != AST_FUN_DECL) continue;
        ctx->current_fun = d;
        sema_enter_scope(ctx);
        /* 参数 */
        for (int j = 0; j < d->u.fun_decl.n_params; j++) {
            ASTNode *p = d->u.fun_decl.params[j];
            Type *t = p->u.param.is_array ? type_int_array(0) : type_int();
            sema_declare(ctx, p->u.param.name, SYM_PARAM, t, p->line);
        }
        /* 函数体 (compound 自带 scope) */
        if (d->u.fun_decl.body) check_compound(ctx, d->u.fun_decl.body);
        sema_leave_scope(ctx);
    }

    /* main 检查 */
    Symbol *main_sym = sema_lookup(ctx, "main");
    if (!main_sym) {
        sema_error(ctx, 0, "no 'main' function defined");
    } else if (main_sym->type->kind != TY_FUNC ||
               main_sym->type->ret->kind != TY_INT ||
               main_sym->type->n_params != 0) {
        sema_error(ctx, 0, "'main' must be 'int main(void)'");
    }

    if (ctx->n_errors == 0)
        fprintf(stderr, "Semantic analysis: OK\n");
    else
        fprintf(stderr, "Semantic analysis: %d errors\n", ctx->n_errors);
}
