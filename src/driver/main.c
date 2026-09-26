/*
 * c1c 主程序
 *
 * 当前实现:
 *   --dump-ast   打印抽象语法树
 *   --dump-tokens 打印 token 流（词法阶段验证）
 *
 * 后续章节会增加:
 *   -S          输出 LLVM IR 文本
 *   -O2         启用优化
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c1.tab.h"  /* bison 生成的 token 定义 */
#include "ast.h"

extern int yyparse(void);
extern ASTNode *program_root;
extern int n_errors;

static void usage(const char *p) {
    fprintf(stderr,
        "Usage: %s [options] < source.c1\n"
        "\n"
        "Options:\n"
        "  --dump-ast     Print the abstract syntax tree\n"
        "  --dump-tokens  Print the token stream\n"
        "  --help         Show this help\n",
        p);
}

int main(int argc, char **argv) {
    int dump_ast = 0, dump_tokens = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump-ast"))    dump_ast = 1;
        else if (!strcmp(argv[i], "--dump-tokens")) dump_tokens = 1;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* 词法模式：仅打印 token */
    if (dump_tokens) {
        extern int yylex(void);
        extern YYSTYPE yylval;
        int t;
        while ((t = yylex()) != 0) {
            const char *name = "?";
            switch (t) {
            case TK_IF: name = "TK_IF"; break;
            case TK_ELSE: name = "TK_ELSE"; break;
            case TK_INT: name = "TK_INT"; break;
            case TK_RETURN: name = "TK_RETURN"; break;
            case TK_VOID: name = "TK_VOID"; break;
            case TK_WHILE: name = "TK_WHILE"; break;
            case TK_ID: name = "TK_ID"; break;
            case TK_NUM: name = "TK_NUM"; break;
            case TK_PLUS: name = "TK_PLUS"; break;
            case TK_MINUS: name = "TK_MINUS"; break;
            case TK_MUL: name = "TK_MUL"; break;
            case TK_DIV: name = "TK_DIV"; break;
            case TK_LE: name = "TK_LE"; break;
            case TK_LT: name = "TK_LT"; break;
            case TK_GE: name = "TK_GE"; break;
            case TK_GT: name = "TK_GT"; break;
            case TK_EQ: name = "TK_EQ"; break;
            case TK_NEQ: name = "TK_NEQ"; break;
            case TK_AND: name = "TK_AND"; break;
            case TK_OR: name = "TK_OR"; break;
            case TK_ASSIGN: name = "TK_ASSIGN"; break;
            case TK_SEMI: name = "TK_SEMI"; break;
            case TK_COMMA: name = "TK_COMMA"; break;
            case TK_LPAREN: name = "TK_LPAREN"; break;
            case TK_RPAREN: name = "TK_RPAREN"; break;
            case TK_LBRACK: name = "TK_LBRACK"; break;
            case TK_RBRACK: name = "TK_RBRACK"; break;
            case TK_LBRACE: name = "TK_LBRACE"; break;
            case TK_RBRACE: name = "TK_RBRACE"; break;
            case TK_ERROR: name = "TK_ERROR"; break;
            }
            if (t == TK_ID)       printf("%-12s %s\n", name, yylval.str);
            else if (t == TK_NUM) printf("%-12s %d\n", name, yylval.num);
            else                  printf("%-12s\n", name);
        }
        return 0;
    }

    if (yyparse() != 0 || n_errors > 0) {
        fprintf(stderr, "Parsing failed (%d errors)\n", n_errors);
        return 1;
    }

    if (dump_ast) {
        ast_print(program_root, 0);
    } else {
        fprintf(stderr, "No action specified. Use --dump-ast or --dump-tokens.\n");
        usage(argv[0]);
        ast_free(program_root);
        return 1;
    }

    ast_free(program_root);
    return 0;
}
