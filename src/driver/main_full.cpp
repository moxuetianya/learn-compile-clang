/*
 * c1c_full —— 完整编译器驱动（第 9-10 章）
 *
 * 前端 (C): 手写 lexer/parser + sema
 * 后端 (C++): LLVM IRBuilder 生成 IR → LLVM 后端出 .o → 调 clang 链接
 *
 * 用法:
 *   c1c_full input.c1                    # 编译+链接成 a.out
 *   c1c_full input.c1 -o fib             # 指定输出名
 *   c1c_full input.c1 --emit-llvm        # 打印 LLVM IR 文本
 *   c1c_full input.c1 --emit-obj -o fib.o
 *   c1c_full input.c1 --opt -o fib       # 第 11 章: mem2reg+instcombine
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

extern "C" {
#include "ast.h"
#include "sema.h"
#include "llvm_codegen.h"

int yyparse(void);
extern ASTNode *program_root;
extern int n_errors;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] input.c1\n"
        "  -o <name>      output name (default a.out)\n"
        "  --emit-llvm    print LLVM IR (.ll) to stdout\n"
        "  --emit-obj     emit object file only, no linking\n"
        "  --opt          run mem2reg/instcombine/simplifycfg (ch.11)\n"
        "  --dump-ast     print AST and exit\n",
        prog);
}

int main(int argc, char **argv) {
    const char *input = nullptr;
    const char *output = "a.out";
    int emit_llvm = 0, emit_obj = 0, optimize = 0, dump_ast = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            output = argv[++i];
        } else if (!strcmp(argv[i], "--emit-llvm")) {
            emit_llvm = 1;
        } else if (!strcmp(argv[i], "--emit-obj")) {
            emit_obj = 1;
        } else if (!strcmp(argv[i], "--opt")) {
            optimize = 1;
        } else if (!strcmp(argv[i], "--dump-ast")) {
            dump_ast = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-' && argv[i][1]) {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        } else {
            input = argv[i];
        }
    }
    if (!input) {
        usage(argv[0]);
        return 1;
    }

    /* 1) 词法+语法 (C 前端) */
    if (freopen(input, "r", stdin) != stdin) {
        fprintf(stderr, "cannot open %s\n", input);
        return 1;
    }
    if (yyparse() != 0 || n_errors > 0) {
        fprintf(stderr, "Parse failed (%d errors)\n", n_errors);
        return 1;
    }

    if (dump_ast) {
        ast_print(program_root, 0);
        return 0;
    }

    /* 2) 语义分析 */
    SemaContext *ctx = sema_new();
    sema_check(ctx, program_root);
    if (ctx->n_errors > 0) {
        fprintf(stderr, "Semantic analysis: %d errors\n", ctx->n_errors);
        sema_free(ctx);
        return 1;
    }
    sema_free(ctx);

    /* 3) 后端 */
    if (emit_llvm)
        return c1_llvm_emit_text(program_root, stdout, optimize);

    const char *obj = emit_obj ? output : "/tmp/c1cXXXXXX.o";
    char tmpl[] = "/tmp/c1cXXXXXX.o";
    if (!emit_obj) {
        int fd = mkstemps(tmpl, 2);
        if (fd < 0) { perror("mkstemps"); return 1; }
        close(fd);
        obj = tmpl;
    }

    if (c1_llvm_emit_object(program_root, obj, optimize) != 0)
        return 1;

    if (emit_obj) return 0;

    /* 4) 链接: 找一个可用的系统链接器 */
    const char *linkers[] = {"clang-18", "clang", "cc", "gcc", nullptr};
    const char *linker = nullptr;
    char probe[128];
    for (int i = 0; linkers[i]; i++) {
        snprintf(probe, sizeof probe, "command -v %s >/dev/null 2>&1", linkers[i]);
        if (system(probe) == 0) { linker = linkers[i]; break; }
    }
    if (!linker) {
        fprintf(stderr, "no linker found (tried clang-18/clang/cc/gcc)\n");
        return 1;
    }

    char cmd[512];
    snprintf(cmd, sizeof cmd, "%s \"%s\" -o \"%s\"", linker, obj, output);
    int rc = system(cmd);
    unlink(obj);
    if (rc != 0) {
        fprintf(stderr, "link failed: %s\n", cmd);
        return 1;
    }
    return 0;
}
