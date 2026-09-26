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
| 1 | MCJIT | 旧，仍能用 |
| 2 | ORC | 当前主流 |
| 3 | ORC v2 / JITLink | 新 |

我们用 ORC。

## 13.3 最小 ORC 程序

```cpp
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/Interpreter.h>
#include <llvm/Support/TargetSelect.h>

int main() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    llvm::LLVMContext ctx;
    llvm::Module *mod = new llvm::Module("jit", ctx);
    llvm::IRBuilder<> b(ctx);

    // int add(int a, int b) { return a + b; }
    auto *i32 = b.getInt32Ty();
    auto *ft = llvm::FunctionType::get(i32, {i32, i32}, false);
    auto *f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "add", mod);
    auto *bb = llvm::BasicBlock::Create(ctx, "entry", f);
    b.SetInsertPoint(bb);
    b.CreateRet(b.CreateAdd(f->getArg(0), f->getArg(1)));

    // 创建 ExecutionEngine
    std::string err;
    auto *ee = llvm::EngineBuilder(std::move(std::unique_ptr<llvm::Module>(mod)))
                  .setErrorStr(&err)
                  .create();
    if (!ee) { llvm::errs() << err; return 1; }

    // 调用 add(40, 2)
    std::vector<llvm::GenericValue> args(2);
    args[0].IntVal = llvm::APInt(32, 40);
    args[1].IntVal = llvm::APInt(32, 2);
    llvm::GenericValue r = ee->runFunction(f, args);
    llvm::outs() << "add(40,2) = " << r.IntVal << "\n";

    return 0;
}
```

```bash
$ clang++ -g jit.cpp `llvm-config --cxxflags --ldflags --libs core executionengine interpreter native` -o jit
$ ./jit
add(40,2) = 42
```

> 注意：`EngineBuilder` 默认选解释器；要 JIT 编译要加 `.setEngineKind(llvm::EngineKind::JIT)`。

## 13.4 真实场景

- 数据库查询执行（每次 query 不同）
- 表达式 DSL（Excel 公式、自定义规则）
- AI 模型推理（XLA、TVM）
- 嵌入式 REPL（编程语言学习环境）

> Clang 自己的 `-fintegrated-cc1` 内部就用 JIT 跑一些 codegen。

## 13.5 小结

- JIT = 运行时编译执行
- LLVM 提供 ExecutionEngine/ORC API
- 教学场景少用，但对理解编译器和写 DSL 极有价值

**练习**：
1. 把第 9 章生成的 IR 直接喂给 ORC，跑 fib(10)。
2. 写一个简单 DSL：`expr = NUMBER | expr + expr`，用 JIT 跑。

---
**下一章**：[第 14 章 期末项目](14-project.md)
