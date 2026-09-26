/*
 * IR 文本输出 driver —— 完整前端流水线: parse → sema → codegen
 *
 * 编译: make c1c_emit
 * 运行: ./build/c1c_emit < fib.c1 > fib.ll
 *       llc-18 fib.ll -filetype=obj -o fib.o && clang-18 fib.o -o fib && ./fib
 */

#include <stdio.h>
#include "ast.h"
#include "sema.h"

extern int yyparse(void);
extern ASTNode *program_root;
extern int n_errors;

extern void gen_program(ASTNode *p);

int main(void) {
    if (yyparse() != 0 || n_errors > 0) {
        fprintf(stderr, "Parse failed (%d errors)\n", n_errors);
        return 1;
    }

    /* 语义分析先行：类型错误不进后端 */
    SemaContext *ctx = sema_new();
    sema_check(ctx, program_root);
    if (ctx->n_errors > 0) {
        fprintf(stderr, "Semantic analysis: %d errors\n", ctx->n_errors);
        sema_free(ctx);
        return 1;
    }
    sema_free(ctx);

    gen_program(program_root);
    ast_free(program_root);
    return 0;
}
