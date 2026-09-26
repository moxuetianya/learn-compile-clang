# 第 11 章：优化入门

> 没有优化的编译器只是"翻译器"。这一章让你看到 LLVM 的优化 Pass 是怎么工作的，并写一个简单的常量折叠 Pass。

## 11.1 优化分哪些层

| 层 | 例子 | 在哪 |
| -- | ---- | ---- |
| 源码级 | `gcc -O3` 帮你内联 | 前端 |
| 中端 | 常量折叠、死代码删除、循环展开 | LLVM IR Pass |
| 后端 | 指令选择、寄存器分配、窥孔 | llc |

我们在中端做。

## 11.2 LLVM Pass 体系

LLVM 用 **Pass 框架** 把每个优化做成一个 Pass，依次跑：

```
Module
  │ FunctionPass
  │   │ BasicBlockPass
  │   │   │ Instruction
  │   └►
  └►
```

Pass 注册：

```cpp
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"

using namespace llvm;

namespace {
struct ConstFoldPass : public FunctionPass {
    static char ID;
    ConstFoldPass() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
        bool changed = false;
        for (auto &BB : F) {
            for (auto it = BB.begin(); it != BB.end(); ) {
                Instruction *I = &*it++;
                if (auto *bo = dyn_cast<BinaryOperator>(I)) {
                    if (auto *C = fold(bo)) {
                        bo->replaceAllUsesWith(C);
                        bo->eraseFromParent();
                        changed = true;
                    }
                }
            }
        }
        return changed;
    }

    Constant* fold(BinaryOperator *bo) {
        auto *L = dyn_cast<ConstantInt>(bo->getOperand(0));
        auto *R = dyn_cast<ConstantInt>(bo->getOperand(1));
        if (!L || !R) return nullptr;
        switch (bo->getOpcode()) {
        case Instruction::Add: return ConstantInt::get(bo->getType(),
                                       L->getValue() + R->getValue());
        case Instruction::Sub: return ConstantInt::get(bo->getType(),
                                       L->getValue() - R->getValue());
        case Instruction::Mul: return ConstantInt::get(bo->getType(),
                                       L->getValue() * R->getValue());
        /* ... */
        default: return nullptr;
        }
    }
};
}

char ConstFoldPass::ID = 0;
static RegisterPass<ConstFoldPass> X("constfold", "Constant Folding");
```

跑：

```bash
opt -constfold fib.ll -o fib.opt.ll -S
```

## 11.3 内置 Pass 列表

`opt --print-passes` 可以看 LLVM 自带的所有 Pass。常用的：

| Pass | 干什么 |
| ---- | ------ |
| `mem2reg` | alloca 提升为 SSA 寄存器 |
| `instcombine` | 指令合并（如 `x+0→x`、`x*1→x`） |
| `simplifycfg` | 控制流简化 |
| `dce` | 死代码删除 |
| `sroa` | 聚合类型标量替换 |
| `licm` | 循环不变量外提 |
| `indvars` | 归纳变量简化 |
| `inline` | 函数内联 |
| `gvn` | 全局值编号（消除冗余 load） |
| `loop-unroll` | 循环展开 |

`opt -O2 fib.ll -S` 等价于依次跑几十个 Pass。

## 11.4 现场常用方法的优劣

| 优化范围 | 难度 | 收益 |
| -------- | ---- | ---- |
| 局部窥孔（peephole） | 低 | 小 |
| 常量传播 / 折叠 | 低 | 中 |
| 死代码删除 | 低 | 中 |
| 内联 | 中 | 高 |
| 循环优化（向量化、展开） | 高 | 高 |
| 自动并行化 | 极高 | 视场景 |
| 寄存器分配 | 极高 | 极高 |

> LLVM 的价值在于：所有这些都已经写好了，Clang 用户直接 `clang -O2` 享受就行。

## 11.5 实战：在 c1c 里跑优化

最简单的：生成 IR 后直接调 `opt`。

```bash
./c1c fib.c1 -o fib.ll
opt -O2 fib.ll -o fib.opt.ll -S
llc fib.opt.ll -o fib.s
clang fib.s -o fib
```

或者在 C++ 内部用 `llvm::PassManager`：

```cpp
llvm::PassManagerBuilder pm;
pm.OptLevel = 2;
pm.populateModulePassManager(mpm);
mpm.run(*mod);
```

## 11.6 几个经典优化的直觉解释

### 11.6.1 常量折叠

```llvm
%a = add i32 2, 3      ⇒  %a = i32 5
```

### 11.6.2 死代码删除

```llvm
%x = add i32 1, 2      ; 没人用
%y = use %x             ⇒  删 %x
```

### 11.6.3 函数内联

```llvm
define i32 @f(i32 %x) { ret i32 %x + 1 }
%r = call i32 @f(i32 10)   ⇒  %r = i32 11
```

### 11.6.4 循环不变量外提（LICM）

```c
for (int i=0; i<n; i++) {
    int t = a*b;     // 提到外面
    c[i] = t + i;
}
```

### 11.6.5 GVN

```c
int t1 = a + b;
int t2 = a + b;     // 第二次不需要重算
```

GVN 找等价的表达式复用。

## 11.7 写一个完整的 Pass 流程

```cpp
int main(int argc, char **argv) {
    // 读 IR
    LLVMContext ctx;
    SMDiagnostic err;
    auto mod = parseIRFile(argv[1], err, ctx);

    // 注册并跑 Pass
    legacy::FunctionPassManager fpm(mod.get());
    fpm.add(new ConstFoldPass());
    fpm.doInitialization();
    for (auto &F : *mod) fpm.run(F);
    fpm.doFinalization();

    // 写回
    mod->print(outs(), nullptr);
}
```

编译：

```bash
clang++ -g constfold.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core passes` -o constfold
```

## 11.8 看优化的效果

```bash
$ opt -O0 fib.ll -S | wc -l
50
$ opt -O2 fib.ll -S | wc -l
30

$ time llc -O0 fib.ll -o /tmp/a.s
real    0m0.2s
$ time llc -O2 fib.ll -o /tmp/b.s
real    0m0.4s
# O2 编译慢但跑得快（这里我们编译 llc 自己，没意义，但能看出 O2 让 llc 工作量大）
```

> 优化让编译器慢但让程序快。

## 11.9 小结

- 优化是 LLVM 强项，核心机制是 Pass
- 自定义 Pass 用 C++ 写，30 行就能实现常量折叠
- 实战中：直接 `opt -O2` 拿现成的几十个 Pass

**练习**：
1. 写一个 Pass：把所有 `add i32 X, 0` 替换成 X。
2. 在 c1c 里加 `--no-opt` 和 `-O2` 选项。
3. 用 `opt -O2 --print-after-all fib.ll` 看每步 Pass 后的 IR。

---
**下一章**：[第 12 章 Clang 架构与插件开发](12-clang-ast.md)
