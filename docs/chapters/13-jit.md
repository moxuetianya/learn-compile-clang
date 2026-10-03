# 第 13 章：JIT 与解释执行（可选）

> 本章可选。介绍 LLVM 的 JIT（Just-In-Time）编译，让你的 c1c 能直接执行 IR 而不是先编译成可执行文件。

## 13.1 什么是 JIT

传统流程：源码 → 编译期 → 可执行文件 → 运行期
JIT：源码 → 编译期 → IR → 运行期边解释边编译

优势：
- 启动延迟可接受
- 运行时可以做 profile-guided optimization
- 适合脚本/DSL/嵌入式场景

劣势：
- 启动比 AOT 慢
- 内存占用大（IR + JIT 引擎）

## 13.2 LLVM JIT 演进

| 代 | 名字 | 状态 |
| -- | ---- | ---- |
| 1 | MCJIT / ExecutionEngine | 旧，废弃（`runFunction`/`GenericValue` 那套） |
| 2 | ORC v1 | 过渡 |
| 3 | **ORC v2 / LLJIT / JITLink** | 当前主流 |

我们用 ORC v2 的 `LLJIT` 高层 API。

## 13.3 最小 ORC 程序（LLJIT）

```cpp
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/TargetSelect.h>

int main() {
    /* 独立进程里必须先注册本机 target —— 没人替你初始化 */
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("jit", ctx);
    llvm::IRBuilder<> b(ctx);

    // int add(int a, int b) { return a + b; }
    auto *ft = llvm::FunctionType::get(b.getInt32Ty(),
                                       {b.getInt32Ty(), b.getInt32Ty()}, false);
    auto *f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                     "add", *mod);
    b.SetInsertPoint(llvm::BasicBlock::Create(ctx, "entry", f));
    b.CreateRet(b.CreateAdd(f->getArg(0), f->getArg(1)));

    // 创建 LLJIT, 移交模块 (Module 引用的类型活在 ctx 里, 两者要一起移交)
    auto jit = llvm::orc::LLJITBuilder().create();
    if (!jit) { llvm::errs() << llvm::toString(jit.takeError()); return 1; }
    llvm::orc::ThreadSafeModule tsm(std::move(mod), std::move(ctx));
    if (auto err = (*jit)->addIRModule(std::move(tsm))) {
        llvm::errs() << llvm::toString(std::move(err));
        return 1;
    }

    // 查找符号拿到函数指针, 直接调用
    auto add = (*jit)->lookup("add");
    if (!add) { llvm::errs() << llvm::toString(add.takeError()); return 1; }
    int r = add->toPtr<int (*)(int, int)>()(40, 2);
    llvm::outs() << "add(40,2) = " << r << "\n";
    return 0;
}
```

```bash
$ clang++ -std=c++17 jit.cpp $(llvm-config --cxxflags --ldflags \
      --libs core orcjit native --system-libs) -o jit
$ ./jit
add(40,2) = 42
```

关键三步：`LLJITBuilder().create()` → `addIRModule` → `lookup`。
和旧 MCJIT 的区别：模块所有权随 `ThreadSafeModule` 移交；符号查找是
**按名字异步解析**的（支持懒编译、按需链接）。

## 13.4 实战：c1c_jit —— 让 C1 程序在进程内跑起来

> 本章实现：[`src/driver/main_jit.cpp`](../../src/driver/main_jit.cpp)。
> 前端/sema/IR 生成与 `c1c_full` 完全共用（`c1_llvm_build_module`
> 把 Module 连同它的 LLVMContext 一起移交给 JIT）。

```bash
$ make c1c_jit
$ ./build/c1c_jit examples/fib.c1
main returned 55
$ ./build/c1c_jit examples/fib.c1 --opt
main returned 55
```

对比 `c1c_full`：**不产出任何文件**——没有 .o、不调链接器，IR 生成后
直接在内存里 JIT 编译执行。这就是数据库/DSL/REPL 场景的形态。

（注意：进程退出码只有 8 位，所以返回值单独打印一行。）

## 13.5 真实场景

- 数据库查询执行（每次 query 不同）
- 表达式 DSL（Excel 公式、自定义规则）
- AI 模型推理（XLA、TVM）
- 嵌入式 REPL（编程语言学习环境）

> Clang 自己的 `-fintegrated-cc1` 内部就用 JIT 跑一些 codegen。

## 13.6 小结

- JIT = 运行时编译执行；LLVM 当代 API 是 ORC LLJIT（MCJIT/ExecutionEngine 已废弃）
- 三步曲：`LLJITBuilder().create()` → `addIRModule` → `lookup`
- 仓库的 `c1c_jit` 复用整套前端，IR 不落盘直接执行

**练习**：
1. （已实现）`c1c_jit examples/fib.c1`——阅读 `src/driver/main_jit.cpp`，
   画出数据流：.c1 → AST → Module → JIT → 函数指针。
2. 给 `c1c_jit` 加一个交互模式：循环读入一行表达式，JIT 编译后立即打印
   结果（迷你 REPL）。提示：每个表达式生成一个 `__expr_N` 函数。
3. 写一个简单 DSL：`expr = NUMBER | expr + expr`，用 LLJIT 跑。

---
**下一章**：[第 14 章 期末项目](14-project.md)
