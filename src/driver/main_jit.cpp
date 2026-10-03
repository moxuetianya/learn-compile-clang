/*
 * c1c_jit —— ORC JIT 驱动（第 13 章）
 *
 * 不产出任何文件: .c1 → AST → LLVM IR → 进程内 JIT 编译 → 直接执行 main。
 * 对比 c1c_full (走磁盘: .o + 链接器), JIT 省掉了整个目标文件/链接环节。
 *
 * 用法:
 *   c1c_jit examples/fib.c1
 *   c1c_jit examples/fib.c1 --opt
 *
 * 用的是 ORC LLJIT (LLVM 当代的 JIT API; MCJIT/ExecutionEngine 已废弃)。
 */

#include <cstdio>
#include <cstring>

extern "C" {
#include "ast.h"
#include "sema.h"
#include "llvm_codegen.h"

int yyparse(void);
extern ASTNode *program_root;
extern int n_errors;
}

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [--opt] input.c1\n"
        "  在进程内 JIT 编译并执行 main, 打印其返回值\n"
        "  --opt   先跑 mem2reg/instcombine/simplifycfg\n",
        prog);
}

int main(int argc, char **argv) {
    const char *input = nullptr;
    int optimize = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--opt")) optimize = 1;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]); return 0;
        }
        else input = argv[i];
    }
    if (!input) { usage(argv[0]); return 1; }

    /* LLJIT 创建前必须注册本机 target —— 独立进程里没有人替我们初始化。
       (c1c_full 在 c1_llvm_emit_object 里做同样的事) */
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    /* 1) 前端: 词法/语法/语义 (与 c1c_full 共用同一套 C 代码) */
    if (freopen(input, "r", stdin) != stdin) {
        fprintf(stderr, "cannot open %s\n", input);
        return 1;
    }
    if (yyparse() != 0 || n_errors > 0) {
        fprintf(stderr, "Parse failed (%d errors)\n", n_errors);
        return 1;
    }
    SemaContext *ctx = sema_new();
    sema_check(ctx, program_root);
    if (ctx->n_errors > 0) {
        fprintf(stderr, "Semantic analysis: %d errors\n", ctx->n_errors);
        sema_free(ctx);
        return 1;
    }
    sema_free(ctx);

    /* 2) 生成 IR (内存中, 不落盘) */
    void *raw_ctx = nullptr, *raw_mod = nullptr;
    if (c1_llvm_build_module(program_root, optimize, &raw_ctx, &raw_mod) != 0)
        return 1;
    auto llctx = std::unique_ptr<llvm::LLVMContext>(
        static_cast<llvm::LLVMContext *>(raw_ctx));
    auto mod = std::unique_ptr<llvm::Module>(
        static_cast<llvm::Module *>(raw_mod));

    /* 3) 创建 LLJIT, 把模块移交给 JIT */
    auto jit = llvm::orc::LLJITBuilder().create();
    if (!jit) {
        llvm::errs() << llvm::toString(jit.takeError());
        return 1;
    }
    llvm::orc::ThreadSafeModule tsm(std::move(mod), std::move(llctx));
    if (auto err = (*jit)->addIRModule(std::move(tsm))) {
        llvm::errs() << llvm::toString(std::move(err));
        return 1;
    }

    /* 4) 查找符号, 拿到函数指针, 直接调用 */
    auto entry = (*jit)->lookup("main");
    if (!entry) {
        llvm::errs() << llvm::toString(entry.takeError());
        return 1;
    }
    auto *main_fn = entry->toPtr<int (*)()>();
    int rc = main_fn();

    /* 进程退出码只有 8 位, fib(10)=55 没问题, 但大值会被截断,
       所以返回值单独打印一行 */
    printf("main returned %d\n", rc);
    return rc & 0xff;
}
