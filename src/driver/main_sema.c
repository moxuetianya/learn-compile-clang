/*
 * 语义分析 driver
 *
 * 编译: gcc src/lexer/handwritten.c src/ast/ast.c src/sema/sema.c src/driver/main_sema.c -o build/c1c_sema
 * 运行: ./build/c1c_sema < fib.c1
 */

#include <stdio.h>
#include "ast.h"
#include "sema.h"

extern int yyparse(void);
extern ASTNode *program_root;
extern int n_errors;

int main(void) {
    if (yyparse() != 0 || n_errors > 0) {
        fprintf(stderr, "Parse failed (%d errors)\n", n_errors);
        return 1;
    }
    SemaContext *ctx = sema_new();
    sema_check(ctx, program_root);
    int rc = ctx->n_errors > 0 ? 1 : 0;
    sema_free(ctx);
    ast_free(program_root);
    return rc;
}
