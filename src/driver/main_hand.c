/*
 * 独立 main, 用于手写 lexer/parser 版本 (handwritten.c)
 *
 * 编译: gcc src/lexer/handwritten.c src/ast/ast.c src/driver/main_hand.c -o build/c1c_hand
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

extern int yyparse(void);
extern ASTNode *program_root;
extern int n_errors;

static void usage(const char *p) {
    fprintf(stderr,
        "Usage: %s --dump-ast < source.c1\n"
        "  --dump-ast  Print abstract syntax tree\n",
        p);
}

int main(int argc, char **argv) {
    int dump_ast = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump-ast")) dump_ast = 1;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]); return 0;
        }
        else { fprintf(stderr, "Unknown: %s\n", argv[i]); return 1; }
    }

    if (!dump_ast) {
        fprintf(stderr, "Need --dump-ast\n");
        usage(argv[0]);
        return 1;
    }

    if (yyparse() != 0 || n_errors > 0) {
        fprintf(stderr, "Parsing failed (%d errors)\n", n_errors);
        return 1;
    }

    ast_print(program_root, 0);
    ast_free(program_root);
    return 0;
}
