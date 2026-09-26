# 第 12 章：Clang 架构与插件开发

> 学完前面 11 章，我们从头写了一个小编译器。本章换个视角：**Clang 是怎么组织的**，以及如何给它写一个插件——这是工业界最常见的扩展方式。

## 12.1 Clang 整体架构

```
                .c / .cpp / .m
                    │
                    ▼
                ┌──────────┐
                │  Parser  │ ← 手写递归下降
                └────┬─────┘
                     │ AST
                     ▼
                ┌──────────┐
                │   Sema   │ ← 语义分析（类型检查、名字解析）
                └────┬─────┘
                     │ Annotated AST
                     ▼
                ┌──────────┐
                │  CodeGen │ ← 翻译成 LLVM IR
                └────┬─────┘
                     │ LLVM IR
                     ▼
                 LLVM 后端
```

特点：
- **手写递归下降**（不用 bison/flex）—— 错误信息可控
- **AST 忠实于源代码**：每个节点链回 token 流
- **ASTConsumer 模式**：可以在 AST 构建完成后插入自定义逻辑
- **Plugin 机制**：动态加载 `.so`，扩展 Clang 行为

## 12.2 Clang AST 一览

打印 AST：

```bash
$ cat test.c
int add(int a, int b) { return a + b; }
$ clang -Xclang -ast-dump -fsyntax-only test.c
TranslationUnitDecl 0x... 
|-FunctionDecl 0x... 'int (int, int)' add
| |-ParmVarDecl 0x... 'a' 'int'
| |-ParmVarDecl 0x... 'b' 'int'
| `-CompoundStmt 0x...
|   `-ReturnStmt 0x...
|     `-BinaryOperator 0x... '+' ...
|       |-ImplicitCastExpr 'int' <LValueToRValue>
|       | `-DeclRefExpr 0x... 'a' 'int'
|       `-ImplicitCastExpr 'int' <LValueToRValue>
|         `-DeclRefExpr 0x... 'b' 'int'
```

注意几个 Clang AST 特色：
1. **保留所有隐式转换**（ImplicitCastExpr）
2. **每个节点有源位置**（可以回溯 token）
3. **模板/重载的 AST 节点非常复杂**

## 12.3 三种扩展方式

### A. LibTooling（推荐入门）

写一个独立程序，调用 Clang 库解析源码。

```cpp
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

using namespace clang;
using namespace clang::tooling;

class FindVarsVisitor : public RecursiveASTVisitor<FindVarsVisitor> {
public:
    bool VisitVarDecl(VarDecl *VD) {
        if (VD->hasGlobalStorage() && VD->getNameAsString() == "x")
            llvm::outs() << "Found global x at "
                         << VD->getLocation().printToString(
                              VD->getASTContext().getSourceManager())
                         << "\n";
        return true;
    }
};

class FindVarsConsumer : public ASTConsumer {
public:
    void HandleTranslationUnit(ASTContext &ctx) override {
        visitor.TraverseDecl(ctx.getTranslationUnitDecl());
    }
private:
    FindVarsVisitor visitor;
};

class FindVarsAction : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer>
    CreateASTConsumer(CompilerInstance &, StringRef) override {
        return std::make_unique<FindVarsConsumer>();
    }
private:
    FindVarsVisitor visitor;
};

static llvm::cl::OptionCategory Cat("find-vars");

int main(int argc, const char **argv) {
    auto op = CommonOptionsParser::create(argc, argv, Cat);
    ClangTool Tool(op->getCompilations(), op->getSourcePathList());
    return Tool.run(newFrontendActionFactory<FindVarsAction>().get());
}
```

CMake：

```cmake
find_package(Clang REQUIRED)
include_directories(${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
add_executable(find_vars find_vars.cpp)
target_link_libraries(find_vars
    clangTooling clangFrontend clangBasic clangAST clangSerialization
    clangDriver clangSema clangAnalysis clangEdit clangRewrite clangLex
    clangParse clangASTMatchers ${LLVM_LIBRARIES})
```

跑：

```bash
$ ./find_vars test.c --
Found global x at test.c:3:5
```

### B. ASTMatcher（DSL）

如果不想手写 visitor，用 Clang 的 matcher DSL：

```cpp
DeclarationMatcher GlobalXMatcher =
    varDecl(hasGlobalStorage(), hasName("x")).bind("globalX");

class XPrinter : public ast_matchers::MatchFinder::MatchCallback {
public:
    void run(const ast_matchers::MatchFinder::MatchResult &r) override {
        const auto *VD = r.Nodes.getNodeAs<VarDecl>("globalX");
        llvm::outs() << "Found: " << VD->getNameAsString() << "\n";
    }
};
```

注册：

```cpp
MatchFinder finder;
finder.addMatcher(GlobalXMatcher, &printer);
finder.matchAST(ctx);
```

### C. Plugin

动态加载：

```cpp
class MyPlugin : public PluginASTAction {
    // ...
};
static FrontendPluginRegistry::Add<MyPlugin>
    X("my-plugin", "my description");
```

```bash
clang -fplugin=./my-plugin.so -Xclang -plugin-my-flag test.c
```

适合 IDE 集成、lint 工具。

## 12.4 三个经典 Clang 工具源码

| 工具 | 用法 |
| ---- | ---- |
| `clang-check` | 看 AST、跳定义 |
| `clang-tidy` | 静态检查（基于 ASTMatcher） |
| `clang-format` | 格式化（基于 Token 重写） |

它们的源码（`clang-tools-extra/`）是学习 Clang AST 编程的最好范例。

## 12.5 实战：写一个 Clang Plugin 找未使用变量

> 用 RecursiveASTVisitor 访问所有 VarDecl，再看是否有被引用。

```cpp
class UnusedVarVisitor : public RecursiveASTVisitor<UnusedVarVisitor> {
public:
    bool VisitVarDecl(VarDecl *VD) {
        if (VD->isLocalVarDecl() && !VD->isUsed() && !VD->hasAttr<UnusedAttr>()) {
            DiagnosticsEngine &diag = ...;
            diag.Report(VD->getLocation(), diag.getCustomDiagID(
                DiagnosticsEngine::Warning, "unused variable '%0'"))
                << VD->getNameAsString();
        }
        return true;
    }
};
```

## 12.6 现场常用方法的优劣

| 场景 | 主流方案 |
| ---- | -------- |
| 公司内部 lint | clang-tidy + 自定义 check（基于 ASTMatcher） |
| IDE 跳转/补全 | libclang C API 或 LibTooling |
| 自动重构 | LibTooling + ASTMatcher + Rewriter |
| 代码统计 | LibTooling + RecursiveASTVisitor |
| 安全审计 | clang static analyzer（符号执行） |

## 12.7 AST 模式总结

```cpp
// 类层次（节选）
Decl
├─ TranslationUnitDecl
├─ NamedDecl
│  ├─ NamespaceDecl
│  ├─ FunctionDecl
│  ├─ VarDecl
│  └─ ParmVarDecl / FieldDecl / ...
└─ ...

Stmt
├─ CompoundStmt
├─ IfStmt / WhileStmt / ForStmt
├─ ReturnStmt
├─ BinaryOperator
├─ UnaryOperator
├─ DeclRefExpr  ← 名字引用
├─ CallExpr
├─ IntegerLiteral / FloatingLiteral
└─ ...

Type
├─ BuiltinType (Int, Float, ...)
├─ PointerType
├─ ArrayType
└─ ...
```

Clang 文档里有完整列表。

## 12.8 小结

- Clang 是手写递归下降 + 强大的 Sema 层 + 多层 AST Consumer 机制
- LibTooling 是写工具的最佳起点
- ASTMatcher 让"找某类节点"变得直观
- Plugin 适合运行时扩展

**练习**：
1. 写一个工具，统计一个 C 文件里每个函数的最深嵌套深度。
2. 用 ASTMatcher 找所有 `malloc` 调用，看有没有匹配的 `free`。
3. 给 `clang-tidy` 写一个 check：禁止使用 `printf` 的 `%n`（安全漏洞）。

---
**下一章**：[第 13 章 JIT 与解释执行](13-jit.md)
