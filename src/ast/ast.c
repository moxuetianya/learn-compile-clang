/*
 * AST 实现
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "tokens.h"

#define NEW_AST() (calloc(1, sizeof(ASTNode)))

/* ---------- 构造器 ---------- */

ASTNode *mk_decl_list(int n, ASTNode *first) {
    ASTNode *p = NEW_AST();
    p->kind = AST_PROGRAM;
    p->u.program.decls = calloc(n, sizeof(ASTNode*));
    p->u.program.decls[0] = first;
    p->u.program.n = n;
    return p;
}

ASTNode *append_decl(ASTNode *list, ASTNode *d) {
    int n = list->u.program.n;
    list->u.program.decls = realloc(list->u.program.decls, (n+1) * sizeof(ASTNode*));
    list->u.program.decls[n] = d;
    list->u.program.n++;
    return list;
}

ASTNode *mk_var_decl(char *name, TypeKind t, int array_size, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_VAR_DECL;
    p->line = line;
    p->type = t;
    p->u.var_decl.name = strdup(name);
    p->u.var_decl.type = t;
    p->u.var_decl.array_size = array_size;
    return p;
}

ASTNode *mk_fun_decl(char *name, TypeKind ret, ASTNode *params, ASTNode *body, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_FUN_DECL;
    p->line = line;
    p->type = T_FUNC;
    p->u.fun_decl.name = strdup(name);
    p->u.fun_decl.ret_type = ret;
    if (params && params->kind == AST_PARAM_LIST) {
        p->u.fun_decl.params = params->u.param_list.items;
        p->u.fun_decl.n_params = params->u.param_list.n;
    } else {
        p->u.fun_decl.params = NULL;
        p->u.fun_decl.n_params = 0;
    }
    p->u.fun_decl.body = body;
    return p;
}

ASTNode *mk_param(char *name, TypeKind t, int is_array, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_PARAM;
    p->line = line;
    p->type = t;
    p->u.param.name = strdup(name);
    p->u.param.type = t;
    p->u.param.is_array = is_array;
    return p;
}

ASTNode *mk_param_list(ASTNode *first) {
    ASTNode *p = NEW_AST();
    p->kind = AST_PARAM_LIST;
    p->u.param_list.items = calloc(1, sizeof(ASTNode*));
    p->u.param_list.items[0] = first;
    p->u.param_list.n = 1;
    return p;
}

ASTNode *append_param(ASTNode *list, ASTNode *p) {
    int n = list->u.param_list.n;
    list->u.param_list.items = realloc(list->u.param_list.items, (n+1) * sizeof(ASTNode*));
    list->u.param_list.items[n] = p;
    list->u.param_list.n++;
    return list;
}

ASTNode *mk_compound(ASTNode *decls, ASTNode *stmts, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_COMPOUND;
    p->line = line;
    p->u.compound.decls = decls ? (ASTNode**)decls->u.program.decls : NULL;
    p->u.compound.nd    = decls ? decls->u.program.n : 0;
    p->u.compound.stmts = stmts ? (ASTNode**)stmts->u.program.decls : NULL;
    p->u.compound.ns    = stmts ? stmts->u.program.n : 0;
    return p;
}

ASTNode *append_local(ASTNode *list, ASTNode *d) {
    if (!list) {
        list = NEW_AST();
        list->kind = AST_PROGRAM;     /* 复用 decl 数组容器 */
        list->u.program.decls = calloc(1, sizeof(ASTNode*));
        list->u.program.decls[0] = d;
        list->u.program.n = 1;
        return list;
    }
    int n = list->u.program.n;
    list->u.program.decls = realloc(list->u.program.decls, (n+1)*sizeof(ASTNode*));
    list->u.program.decls[n] = d;
    list->u.program.n++;
    return list;
}

ASTNode *append_stmt(ASTNode *list, ASTNode *s) {
    return append_local(list, s);
}

ASTNode *mk_expr_stmt(ASTNode *e, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_EXPR_STMT;
    p->line = line;
    p->u.expr_stmt.expr = e;
    return p;
}

ASTNode *mk_if(ASTNode *cond, ASTNode *then_br, ASTNode *else_br, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_IF;
    p->line = line;
    p->u.if_stmt.cond = cond;
    p->u.if_stmt.then_br = then_br;
    p->u.if_stmt.else_br = else_br;
    return p;
}

ASTNode *mk_while(ASTNode *cond, ASTNode *body, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_WHILE;
    p->line = line;
    p->u.while_stmt.cond = cond;
    p->u.while_stmt.body = body;
    return p;
}

ASTNode *mk_ret(ASTNode *e, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_RETURN;
    p->line = line;
    p->u.ret_stmt.expr = e;
    return p;
}

ASTNode *mk_assign(ASTNode *var, ASTNode *expr, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_ASSIGN;
    p->line = line;
    p->u.assign.var = var;
    p->u.assign.expr = expr;
    return p;
}

ASTNode *mk_binop(int op, ASTNode *lhs, ASTNode *rhs, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_BINOP;
    p->line = line;
    p->u.binop.op = op;
    p->u.binop.lhs = lhs;
    p->u.binop.rhs = rhs;
    return p;
}

ASTNode *mk_var(char *name, ASTNode *idx, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_VAR;
    p->line = line;
    p->u.var.name = strdup(name);
    p->u.var.index = idx;
    return p;
}

ASTNode *mk_call(char *name, ASTNode *args, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_CALL;
    p->line = line;
    p->u.call.name = strdup(name);
    if (args) {
        p->u.call.args = (ASTNode**)args->u.program.decls;
        p->u.call.n = args->u.program.n;
    } else {
        p->u.call.args = NULL;
        p->u.call.n = 0;
    }
    return p;
}

ASTNode *mk_num(int value, int line) {
    ASTNode *p = NEW_AST();
    p->kind = AST_NUM;
    p->line = line;
    p->type = T_INT;
    p->u.num.value = value;
    return p;
}

ASTNode *mk_arg_list(ASTNode *first) {
    ASTNode *p = NEW_AST();
    p->kind = AST_PROGRAM;     /* 复用 decl 数组 */
    p->u.program.decls = calloc(1, sizeof(ASTNode*));
    p->u.program.decls[0] = first;
    p->u.program.n = 1;
    return p;
}

ASTNode *append_arg(ASTNode *list, ASTNode *e) {
    return append_local(list, e);
}

/* ---------- 输出 ---------- */

const char *kind_name(ASTKind k) {
    switch (k) {
    case AST_PROGRAM:   return "Program";
    case AST_VAR_DECL:  return "VarDecl";
    case AST_FUN_DECL:  return "FunDecl";
    case AST_PARAM:     return "Param";
    case AST_PARAM_LIST:return "ParamList";
    case AST_COMPOUND:  return "Compound";
    case AST_EXPR_STMT: return "ExprStmt";
    case AST_IF:        return "If";
    case AST_WHILE:     return "While";
    case AST_RETURN:    return "Return";
    case AST_ASSIGN:    return "Assign";
    case AST_BINOP:     return "BinOp";
    case AST_VAR:       return "Var";
    case AST_CALL:      return "Call";
    case AST_NUM:       return "Num";
    }
    return "?";
}

const char *type_name(TypeKind t) {
    switch (t) {
    case T_INT:       return "int";
    case T_VOID:      return "void";
    case T_INT_ARRAY: return "int[]";
    case T_FUNC:      return "function";
    }
    return "?";
}

static const char *op_name(int op) {
    /* 接受多种 token 编码:
     *   手写 lexer: HW_TK_* (数值从 1000 起)
     *   tokens.h:   TK_PLUS=9, TK_MINUS=10... (手写 lexer 编译 ast.c 时)
     *   bison:      TK_PLUS=266... (bison 自动分配的 token 数值)
     */
    switch (op) {
    case HW_TK_PLUS:   case TK_PLUS:   return "+";
    case HW_TK_MINUS:  case TK_MINUS:  return "-";
    case HW_TK_MUL:    case TK_MUL:    return "*";
    case HW_TK_DIV:    case TK_DIV:    return "/";
    case HW_TK_LT:     case TK_LT:     return "<";
    case HW_TK_LE:     case TK_LE:     return "<=";
    case HW_TK_GT:     case TK_GT:     return ">";
    case HW_TK_GE:     case TK_GE:     return ">=";
    case HW_TK_EQ:     case TK_EQ:     return "==";
    case HW_TK_NEQ:    case TK_NEQ:    return "!=";
    case HW_TK_AND:    case TK_AND:    return "&&";
    case HW_TK_OR:     case TK_OR:     return "||";
    case HW_TK_ASSIGN: case TK_ASSIGN: return "=";
    /* bison 路径下的 token 数值 (与上面 TK_* 同名但数值不同) */
    case 266: return "+";
    case 267: return "-";
    case 268: return "*";
    case 269: return "/";
    case 270: return "<=";
    case 271: return "<";
    case 272: return ">=";
    case 273: return ">";
    case 274: return "==";
    case 275: return "!=";
    case 276: return "&&";
    case 277: return "||";
    case 278: return "=";
    }
    return "?";
}

static void print_indent(int n) { for (int i = 0; i < n; i++) putchar(' '); }

void ast_print(ASTNode *n, int indent) {
    if (!n) return;
    print_indent(indent);
    switch (n->kind) {
    case AST_PROGRAM:
        printf("Program\n");
        for (int i = 0; i < n->u.program.n; i++) ast_print(n->u.program.decls[i], indent + 2);
        break;
    case AST_VAR_DECL:
        if (n->u.var_decl.array_size)
            printf("VarDecl %s %s[%d] (line %d)\n",
                   type_name(n->u.var_decl.type),
                   n->u.var_decl.name,
                   n->u.var_decl.array_size,
                   n->line);
        else
            printf("VarDecl %s %s (line %d)\n",
                   type_name(n->u.var_decl.type), n->u.var_decl.name, n->line);
        break;
    case AST_FUN_DECL:
        printf("FunDecl %s %s(...) (line %d)\n",
               type_name(n->u.fun_decl.ret_type), n->u.fun_decl.name, n->line);
        ast_print(n->u.fun_decl.body, indent + 2);
        break;
    case AST_PARAM_LIST:
        for (int i = 0; i < n->u.param_list.n; i++) ast_print(n->u.param_list.items[i], indent);
        break;
    case AST_PARAM:
        printf("%sParam %s%s\n",
               indent ? "" : "",
               n->u.param.name,
               n->u.param.is_array ? "[]" : "");
        break;
    case AST_COMPOUND:
        printf("Compound (line %d)\n", n->line);
        for (int i = 0; i < n->u.compound.nd; i++) ast_print(n->u.compound.decls[i], indent + 2);
        for (int i = 0; i < n->u.compound.ns; i++) ast_print(n->u.compound.stmts[i], indent + 2);
        break;
    case AST_EXPR_STMT:
        printf("ExprStmt\n");
        ast_print(n->u.expr_stmt.expr, indent + 2);
        break;
    case AST_IF:
        printf("If\n");
        ast_print(n->u.if_stmt.cond, indent + 2);
        ast_print(n->u.if_stmt.then_br, indent + 2);
        if (n->u.if_stmt.else_br) {
            print_indent(indent); printf("Else\n");
            ast_print(n->u.if_stmt.else_br, indent + 2);
        }
        break;
    case AST_WHILE:
        printf("While\n");
        ast_print(n->u.while_stmt.cond, indent + 2);
        ast_print(n->u.while_stmt.body, indent + 2);
        break;
    case AST_RETURN:
        printf("Return\n");
        if (n->u.ret_stmt.expr) ast_print(n->u.ret_stmt.expr, indent + 2);
        break;
    case AST_ASSIGN:
        printf("Assign\n");
        ast_print(n->u.assign.var, indent + 2);
        ast_print(n->u.assign.expr, indent + 2);
        break;
    case AST_BINOP:
        printf("BinOp %s\n", op_name(n->u.binop.op));
        ast_print(n->u.binop.lhs, indent + 2);
        ast_print(n->u.binop.rhs, indent + 2);
        break;
    case AST_VAR:
        if (n->u.var.index) {
            printf("Var %s[\n", n->u.var.name);
            ast_print(n->u.var.index, indent + 2);
            print_indent(indent + 1); printf("]\n");
        } else
            printf("Var %s\n", n->u.var.name);
        break;
    case AST_CALL:
        printf("Call %s\n", n->u.call.name);
        for (int i = 0; i < n->u.call.n; i++) ast_print(n->u.call.args[i], indent + 2);
        break;
    case AST_NUM:
        printf("Num %d\n", n->u.num.value);
        break;
    default:
        printf("(unknown)\n");
    }
}

/* ---------- 释放 ---------- */

void ast_free(ASTNode *n) {
    if (!n) return;
    switch (n->kind) {
    case AST_PROGRAM:
        for (int i = 0; i < n->u.program.n; i++) ast_free(n->u.program.decls[i]);
        free(n->u.program.decls);
        break;
    case AST_VAR_DECL:
        free(n->u.var_decl.name);
        break;
    case AST_FUN_DECL:
        free(n->u.fun_decl.name);
        if (n->u.fun_decl.params) {
            for (int i = 0; i < n->u.fun_decl.n_params; i++)
                ast_free(n->u.fun_decl.params[i]);
            free(n->u.fun_decl.params);
        }
        ast_free(n->u.fun_decl.body);
        break;
    case AST_PARAM_LIST:
        for (int i = 0; i < n->u.param_list.n; i++) ast_free(n->u.param_list.items[i]);
        free(n->u.param_list.items);
        break;
    case AST_PARAM:
        free(n->u.param.name);
        break;
    case AST_COMPOUND:
        for (int i = 0; i < n->u.compound.nd; i++) ast_free(n->u.compound.decls[i]);
        free(n->u.compound.decls);
        for (int i = 0; i < n->u.compound.ns; i++) ast_free(n->u.compound.stmts[i]);
        free(n->u.compound.stmts);
        break;
    case AST_EXPR_STMT: ast_free(n->u.expr_stmt.expr); break;
    case AST_IF:        ast_free(n->u.if_stmt.cond); ast_free(n->u.if_stmt.then_br); ast_free(n->u.if_stmt.else_br); break;
    case AST_WHILE:     ast_free(n->u.while_stmt.cond); ast_free(n->u.while_stmt.body); break;
    case AST_RETURN:    ast_free(n->u.ret_stmt.expr); break;
    case AST_ASSIGN:    ast_free(n->u.assign.var); ast_free(n->u.assign.expr); break;
    case AST_BINOP:     ast_free(n->u.binop.lhs); ast_free(n->u.binop.rhs); break;
    case AST_VAR:       free(n->u.var.name); ast_free(n->u.var.index); break;
    case AST_CALL:      free(n->u.call.name); for (int i = 0; i < n->u.call.n; i++) ast_free(n->u.call.args[i]); free(n->u.call.args); break;
    case AST_NUM:       break;
    }
    free(n);
}
