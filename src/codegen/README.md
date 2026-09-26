# src/codegen/

> 第 9-10 章会替换为基于 LLVM C++ API 的实现。这里有一个**教学版** —— 直接打印 LLVM IR 文本，用来快速验证 AST 的形状对不对。

## 当前限制

- 函数控制流的 `if`/`while` 翻译尚不完整：then 分支有 `ret` 后还会生成 `br end`，导致 IR 不合法。
- 函数调用参数打印有小 bug。
- 不做类型检查、作用域检查。

## LLVM C++ API 版本（推荐）

见 [第 9 章 文档](../../docs/chapters/09-ir-llvm.md)。计划步骤：

```cpp
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

llvm::LLVMContext ctx;
llvm::Module mod("c1c", ctx);
llvm::IRBuilder<> builder(ctx);

// AST -> IR
// 用 IRBuilder::CreateAlloca / CreateLoad / CreateStore / CreateAdd ...
```

## 路线图

| 章节 | 内容 |
| ---- | ---- |
| 第 9 章 | LLVM IR 文本输出 → 改用 LLVM C++ API |
| 第 10 章 | 链接可执行文件 |
| 第 11 章 | 写常量折叠 Pass |
