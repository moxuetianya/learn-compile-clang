/*
 * IR 文本输出 driver
 *
 * 编译: gcc src/lexer/handwritten.c src/ast/ast.c src/codegen/codegen.c src/driver/main_emit.c -o build/c1c_emit
 * 运行: ./build/c1c_emit < fib.c1 > fib.ll
 *       llc-18 fib.ll -o fib.s && clang-18 fib.s -o fib && ./fib
 */

#include <stdio.h>
#include "ast.h"

extern int yyparse(void);
extern ASTNode *program_root;
extern int n_errors;
extern void gen_program(ASTNode *p);

int main(void) {
    if (yyparse() != 0 || n_errors > 0) {
        fprintf(stderr, "Parse failed (%d errors)\n", n_errors);
        return 1;
    }
    gen_program(program_root);
    ast_free(program_root);
    return 0;
}
