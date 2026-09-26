/*
 * 纯 C 手写词法/语法分析器 —— flex/bison 的备用方案
 *
 * 在没有 flex/bison 的环境（如受限的容器）也能跑。
 *
 * 编译: gcc src/lexer/handwritten.c src/ast/ast.c src/driver/main.c -o build/c1c_hand
 * 运行: ./build/c1c_hand --dump-ast < examples/fib.c1
 *
 * 这个文件实现完整的 C1 词法分析器 + 递归下降语法分析器。
 * 不是为了替代 flex/bison —— 那些章节会讲得更清楚。
 * 这里的存在只是为了"任何环境都能编译跑起来"。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ast.h"
#include "tokens.h"

ASTNode *program_root = NULL;
int n_errors = 0;

/* 把 TK_* (tokens.h 中的通用 enum) 映射到 HW_* (本地编码, 错开避免与 bison 冲突) */
#define TK_IF     HW_TK_IF
#define TK_ELSE   HW_TK_ELSE
#define TK_INT    HW_TK_INT
#define TK_RETURN HW_TK_RETURN
#define TK_VOID   HW_TK_VOID
#define TK_WHILE  HW_TK_WHILE
#define TK_ID     HW_TK_ID
#define TK_NUM    HW_TK_NUM
#define TK_PLUS   HW_TK_PLUS
#define TK_MINUS  HW_TK_MINUS
#define TK_MUL    HW_TK_MUL
#define TK_DIV    HW_TK_DIV
#define TK_LT     HW_TK_LT
#define TK_LE     HW_TK_LE
#define TK_GT     HW_TK_GT
#define TK_GE     HW_TK_GE
#define TK_EQ     HW_TK_EQ
#define TK_NEQ    HW_TK_NEQ
#define TK_AND    HW_TK_AND
#define TK_OR     HW_TK_OR
#define TK_ASSIGN HW_TK_ASSIGN
#define TK_SEMI   HW_TK_SEMI
#define TK_COMMA  HW_TK_COMMA
#define TK_LPAREN HW_TK_LPAREN
#define TK_RPAREN HW_TK_RPAREN
#define TK_LBRACK HW_TK_LBRACK
#define TK_RBRACK HW_TK_RBRACK
#define TK_LBRACE HW_TK_LBRACE
#define TK_RBRACE HW_TK_RBRACE
#define TK_EOF    HW_TK_EOF
#define TK_ERROR  HW_TK_ERROR

typedef struct { int num; char *str; } YYSTYPE_value;
static YYSTYPE_value yylval;

/* ---------------- 词法 ---------------- */
/* Tok enum 定义在 ast.h */

typedef struct {
    int tok;
    int num;        /* NUM 值 */
    char *str;      /* ID 名 */
    int line;
} Token;

static Token cur;
static FILE *yyin = NULL;
static int   yylineno = 1;

static void next_token(void) {
    int c;
    /* skip ws */
    while ((c = fgetc(yyin)) != EOF) {
        if (c == '\n') { yylineno++; continue; }
        if (isspace(c)) continue;
        if (c == '/') {
            int c2 = fgetc(yyin);
            if (c2 == '*') {
                /* comment */
                int prev = 0;
                while ((c2 = fgetc(yyin)) != EOF) {
                    if (c2 == '\n') yylineno++;
                    if (prev == '*' && c2 == '/') break;
                    prev = c2;
                }
                continue;
            }
            ungetc(c2, yyin);
            break;
        }
        break;
    }

    cur.line = yylineno;

    if (c == EOF) { cur.tok = TK_EOF; return; }

    if (isalpha(c)) {
        char buf[64]; int n = 0;
        buf[n++] = c;
        while ((c = fgetc(yyin)) != EOF && (isalnum(c))) buf[n++] = c;
        buf[n] = 0;
        if (c != EOF) ungetc(c, yyin);

        if      (!strcmp(buf, "if"))     cur.tok = TK_IF;
        else if (!strcmp(buf, "else"))   cur.tok = TK_ELSE;
        else if (!strcmp(buf, "int"))    cur.tok = TK_INT;
        else if (!strcmp(buf, "return")) cur.tok = TK_RETURN;
        else if (!strcmp(buf, "void"))   cur.tok = TK_VOID;
        else if (!strcmp(buf, "while"))  cur.tok = TK_WHILE;
        else {
            cur.tok = TK_ID;
            cur.str = strdup(buf);
        }
        return;
    }

    if (isdigit(c)) {
        char buf[32]; int n = 0;
        buf[n++] = c;
        while ((c = fgetc(yyin)) != EOF && isdigit(c)) buf[n++] = c;
        buf[n] = 0;
        if (c != EOF) ungetc(c, yyin);
        cur.tok = TK_NUM;
        cur.num = atoi(buf);
        return;
    }

    /* 多字符操作符优先 */
    if (c == '<') { if ((c=fgetc(yyin)) == '=') { cur.tok = TK_LE; return; } ungetc(c,yyin); cur.tok = TK_LT; return; }
    if (c == '>') { if ((c=fgetc(yyin)) == '=') { cur.tok = TK_GE; return; } ungetc(c,yyin); cur.tok = TK_GT; return; }
    if (c == '=') { if ((c=fgetc(yyin)) == '=') { cur.tok = TK_EQ; return; } ungetc(c,yyin); cur.tok = TK_ASSIGN; return; }
    if (c == '!') { if ((c=fgetc(yyin)) == '=') { cur.tok = TK_NEQ; return; } ungetc(c,yyin); cur.tok = TK_ERROR; return; }
    if (c == '&') { if ((c=fgetc(yyin)) == '&') { cur.tok = TK_AND; return; } ungetc(c,yyin); cur.tok = TK_ERROR; return; }
    if (c == '|') { if ((c=fgetc(yyin)) == '|') { cur.tok = TK_OR; return; } ungetc(c,yyin); cur.tok = TK_ERROR; return; }

    switch (c) {
    case '+': cur.tok = TK_PLUS; return;
    case '-': cur.tok = TK_MINUS; return;
    case '*': cur.tok = TK_MUL; return;
    case '/': cur.tok = TK_DIV; return;
    case ';': cur.tok = TK_SEMI; return;
    case ',': cur.tok = TK_COMMA; return;
    case '(': cur.tok = TK_LPAREN; return;
    case ')': cur.tok = TK_RPAREN; return;
    case '[': cur.tok = TK_LBRACK; return;
    case ']': cur.tok = TK_RBRACK; return;
    case '{': cur.tok = TK_LBRACE; return;
    case '}': cur.tok = TK_RBRACE; return;
    }
    cur.tok = TK_ERROR;
}

static int match(int t) {
    if (cur.tok == t) { next_token(); return 1; }
    fprintf(stderr, "Parse error at line %d: expected token %d got %d\n",
            cur.line, t, cur.tok);
    n_errors++;
    return 0;
}

/* ---------------- 语法（递归下降）---------------- */

static ASTNode *parse_expr(void);
static ASTNode *parse_simple_expr(void);
static ASTNode *parse_add_expr(void);
static ASTNode *parse_mul_expr(void);
static ASTNode *parse_primary(void);
static ASTNode *parse_call_continue(char *name);
static ASTNode *parse_stmt(void);
static ASTNode *parse_compound(void);
static ASTNode *parse_decl(void);
static ASTNode *parse_args(void);
static TypeKind parse_type_spec_kind(void);
static int parse_param_one(ASTNode **out);

static ASTNode *parse_args(void) {
    ASTNode *list = NULL;
    if (cur.tok == TK_RPAREN) return NULL;
    list = mk_arg_list(parse_expr());
    while (cur.tok == TK_COMMA) {
        next_token();
        list = append_arg(list, parse_expr());
    }
    return list;
}

static ASTNode *parse_call(void) {
    /* 备用 — 实际未使用 (ID 分支直接调 parse_call_continue) */
    return NULL;
}

static ASTNode *parse_primary(void) {
    ASTNode *r;
    /* 一元负号 */
    if (cur.tok == TK_MINUS) {
        next_token();
        ASTNode *p = parse_primary();
        return mk_binop(TK_MINUS, mk_num(0, cur.line), p, cur.line);
    }
    if (cur.tok == TK_LPAREN) {
        next_token();
        r = parse_expr();
        match(TK_RPAREN);
        return r;
    }
    if (cur.tok == TK_NUM) {
        r = mk_num(cur.num, cur.line);
        next_token();
        return r;
    }
    if (cur.tok == TK_ID) {
        char *name = cur.str;
        next_token();
        if (cur.tok == TK_LPAREN) {
            return parse_call_continue(name);
        }
        /* 否则是 var */
        ASTNode *idx = NULL;
        if (cur.tok == TK_LBRACK) {
            next_token();
            idx = parse_expr();
            match(TK_RBRACK);
        }
        return mk_var(name, idx, cur.line);
    }
    fprintf(stderr, "Parse error at line %d: unexpected token in primary\n", cur.line);
    n_errors++;
    return NULL;
}

static ASTNode *parse_call_continue(char *name) {
    /* name '(' already seen */
    match(TK_LPAREN);
    ASTNode *args = parse_args();
    match(TK_RPAREN);
    return mk_call(name, args, cur.line);
}

static ASTNode *parse_var(void) {
    /* 备用 — 实际未使用 */
    return NULL;
}

static ASTNode *parse_mul_expr(void) {
    ASTNode *l = parse_primary();
    while (cur.tok == TK_MUL || cur.tok == TK_DIV) {
        int op = cur.tok; next_token();
        ASTNode *r = parse_primary();
        l = mk_binop(op, l, r, cur.line);
    }
    return l;
}

static ASTNode *parse_add_expr(void) {
    ASTNode *l = parse_mul_expr();
    while (cur.tok == TK_PLUS || cur.tok == TK_MINUS) {
        int op = cur.tok; next_token();
        ASTNode *r = parse_mul_expr();
        l = mk_binop(op, l, r, cur.line);
    }
    return l;
}

static int is_relop(int t) {
    return t==TK_LE||t==TK_LT||t==TK_GE||t==TK_GT||t==TK_EQ||t==TK_NEQ||t==TK_AND||t==TK_OR;
}

static ASTNode *parse_simple_expr(void) {
    ASTNode *l = parse_add_expr();
    if (is_relop(cur.tok)) {
        int op = cur.tok; next_token();
        ASTNode *r = parse_add_expr();
        return mk_binop(op, l, r, cur.line);
    }
    return l;
}

static ASTNode *parse_expr(void) {
    /* 语法: expr ::= simple_expr | ID '=' expr | ID '[' expr ']' (lvalue contexts)
     * 注意 C1 的赋值是右结合, 但优先级最高.
     * 这里用 ID 后跟 ( = [ 才走 ID 路径, 否则走 simple_expr.
     * 关键问题: 词法已吃掉 c, 我们无法回退.
     * 解决方案: 在 ID 分支里看到 +, -, *, /, <, >, ==, ... 时,
     *           不能简单返回 var, 否则后续 +b 无法被解析.
     * 折中: parse_expr 永远 parse_simple_expr 起步,
     *        但当看到 ID 时进入"前瞻 ID" 逻辑:
     *          如果 ID 后面是 (, [ 或 =: 在 ID 上处理
     *          否则: 回到 parse_primary 风格, 让 ID 作为 primary 之一
     */
    if (cur.tok == TK_ID) {
        char *name = cur.str;
        int line = cur.line;
        /* 偷看: 把 next token 也存起来 */
        next_token();
        ASTNode *l;
        if (cur.tok == TK_LPAREN) {
            l = parse_call_continue(name);
        } else if (cur.tok == TK_LBRACK) {
            next_token();
            ASTNode *idx = parse_expr();
            match(TK_RBRACK);
            l = mk_var(name, idx, line);
        } else {
            l = mk_var(name, NULL, line);
        }
        /* 检查 = (赋值) */
        if (cur.tok == TK_ASSIGN) {
            next_token();
            ASTNode *e = parse_expr();
            return mk_assign(l, e, line);
        }
        /* 处理后续 * / + - 等二元运算 */
        while (cur.tok == TK_MUL || cur.tok == TK_DIV) {
            int op = cur.tok; next_token();
            ASTNode *r = parse_primary();
            l = mk_binop(op, l, r, cur.line);
        }
        while (cur.tok == TK_PLUS || cur.tok == TK_MINUS) {
            int op = cur.tok; next_token();
            ASTNode *r = parse_mul_expr();
            l = mk_binop(op, l, r, cur.line);
        }
        if (is_relop(cur.tok)) {
            int op = cur.tok; next_token();
            ASTNode *r = parse_add_expr();
            l = mk_binop(op, l, r, cur.line);
        }
        return l;
    }
    return parse_simple_expr();
}

static ASTNode *parse_return_stmt(void) {
    next_token(); /* return */
    if (cur.tok == TK_SEMI) {
        next_token();
        return mk_ret(NULL, cur.line);
    }
    ASTNode *e = parse_expr();
    match(TK_SEMI);
    return mk_ret(e, cur.line);
}

static ASTNode *parse_stmt(void) {
    if (cur.tok == TK_SEMI) {
        next_token();
        return NULL;
    }
    if (cur.tok == TK_IF) {
        next_token();
        match(TK_LPAREN);
        ASTNode *c = parse_expr();
        match(TK_RPAREN);
        ASTNode *t = parse_stmt();
        ASTNode *e = NULL;
        if (cur.tok == TK_ELSE) {
            next_token();
            e = parse_stmt();
        }
        return mk_if(c, t, e, cur.line);
    }
    if (cur.tok == TK_WHILE) {
        next_token();
        match(TK_LPAREN);
        ASTNode *c = parse_expr();
        match(TK_RPAREN);
        ASTNode *b = parse_stmt();
        return mk_while(c, b, cur.line);
    }
    if (cur.tok == TK_RETURN) {
        return parse_return_stmt();
    }
    if (cur.tok == TK_LBRACE) {
        return parse_compound();
    }
    /* expression statement */
    ASTNode *e = parse_expr();
    match(TK_SEMI);
    return mk_expr_stmt(e, cur.line);
}

static ASTNode *parse_compound(void) {
    int line = cur.line;
    next_token(); /* { */
    ASTNode *decls = NULL;
    ASTNode *stmts = NULL;
    while (cur.tok == TK_INT || cur.tok == TK_VOID) {
        /* local var decl */
        int decl_line = cur.line;
        TypeKind t;
        if (cur.tok == TK_INT) { t = T_INT; next_token(); }
        else { t = T_VOID; next_token(); }
        char *name = cur.str;
        match(TK_ID);
        ASTNode *size = NULL;
        if (cur.tok == TK_LBRACK) {
            next_token();
            size = mk_num(cur.num, cur.line);
            match(TK_NUM);
            match(TK_RBRACK);
        }
        match(TK_SEMI);
        decls = append_local(decls, mk_var_decl(name, t, size ? size->u.num.value : 0, decl_line));
    }
    while (cur.tok != TK_RBRACE && cur.tok != TK_EOF) {
        stmts = append_stmt(stmts, parse_stmt());
    }
    match(TK_RBRACE);
    return mk_compound(decls, stmts, line);
}

static TypeKind parse_type_spec_kind(void) {
    TypeKind t;
    if (cur.tok == TK_INT) { t = T_INT; next_token(); }
    else if (cur.tok == TK_VOID) { t = T_VOID; next_token(); }
    else { fprintf(stderr, "expected type at line %d\n", cur.line); n_errors++; return T_INT; }
    return t;
}

static int parse_param_one(ASTNode **out) {
    int line = cur.line;
    TypeKind pt = parse_type_spec_kind();
    if (cur.tok != TK_ID) { fprintf(stderr, "expected param name at line %d\n", cur.line); n_errors++; return 0; }
    char *pn = cur.str; next_token();
    int is_arr = 0;
    if (cur.tok == TK_LBRACK) { next_token(); match(TK_RBRACK); is_arr = 1; }
    *out = mk_param(pn, pt, is_arr, line);
    return 1;
}

static ASTNode *parse_decl(void) {
    TypeKind t;
    int line = cur.line;   /* 记录 decl 起始行 */
    if (cur.tok == TK_INT) { t = T_INT; next_token(); }
    else if (cur.tok == TK_VOID) { t = T_VOID; next_token(); }
    else { fprintf(stderr, "expected type at line %d\n", cur.line); n_errors++; return NULL; }
    char *name = cur.str;
    match(TK_ID);
    if (cur.tok == TK_LPAREN) {
        /* function */
        next_token();
        ASTNode *params = NULL;
        if (cur.tok != TK_RPAREN) {
            if (cur.tok == TK_VOID) {
                next_token();
                params = NULL;
            } else {
                /* parse param list */
                ASTNode *first = NULL;
                if (!parse_param_one(&first)) return NULL;
                params = mk_param_list(first);
                while (cur.tok == TK_COMMA) {
                    next_token();
                    ASTNode *nx = NULL;
                    if (!parse_param_one(&nx)) return NULL;
                    params = append_param(params, nx);
                }
            }
        }
        match(TK_RPAREN);
        ASTNode *body = parse_compound();
        return mk_fun_decl(name, t, params, body, line);
    }
    /* variable declaration */
    ASTNode *size = NULL;
    if (cur.tok == TK_LBRACK) {
        next_token();
        size = mk_num(cur.num, cur.line);
        match(TK_NUM);
        match(TK_RBRACK);
    }
    match(TK_SEMI);
    return mk_var_decl(name, t, size ? size->u.num.value : 0, line);
}

/* ---------------- main ---------------- */

int yyparse(void) {
    if (!yyin) yyin = stdin;
    next_token();
    ASTNode *prog = NULL;
    while (cur.tok != TK_EOF) {
        prog = append_local(prog, parse_decl());
    }
    program_root = prog;
    return n_errors > 0 ? 1 : 0;
}
