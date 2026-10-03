/*
 * unused-vars —— Clang 插件: 找出从未被引用的局部变量（第 12 章实战）
 *
 * 构建: make clang-plugin
 * 使用:
 *   clang-18 -fplugin=build/unused-vars.so -fsyntax-only file.c
 *
 * 三件套 (Clang 插件的标准骨架):
 *   RecursiveASTVisitor  遍历 AST, 收集 VarDecl(声明) 和 DeclRefExpr(引用)
 *   ASTConsumer          一个翻译单元解析完成时被回调
 *   PluginASTAction      插件入口, 注册到 FrontendPluginRegistry
 *
 * 局限 (留作练习): 只报告"从未被引用"的变量; unused-but-set
 * (int x; x = 5;) 算被引用, 因为赋值左边也是 DeclRefExpr。要区分
 * 读写需要检查表达式的值类别 (Expr::isLValue() / 被 ImplicitCastExpr
 * 包裹的情况)。
 */

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>

#include <set>
#include <vector>

using namespace clang;

namespace {

class UnusedVarsVisitor : public RecursiveASTVisitor<UnusedVarsVisitor> {
public:
    explicit UnusedVarsVisitor(ASTContext &ctx) : ctx(ctx) {}

    /* 1) 收集所有函数内局部变量声明 */
    bool VisitVarDecl(VarDecl *VD) {
        if (isa<ParmVarDecl>(VD))            return true;  /* 参数不查 */
        if (!VD->isLocalVarDecl())           return true;  /* 只查局部 */
        if (VD->getStorageClass() == SC_Static) return true;
        if (!VD->getLocation().isValid())    return true;
        if (VD->isImplicit())                return true;
        decls.push_back(VD);
        return true;
    }

    /* 2) 收集所有对变量的引用 (读或写都算) */
    bool VisitDeclRefExpr(DeclRefExpr *E) {
        if (auto *VD = dyn_cast<VarDecl>(E->getDecl()))
            used.insert(VD->getCanonicalDecl());
        return true;
    }

    /* 3) 差集 = 从未被引用 → 通过诊断引擎告警 (带源码位置)
     *
     * 坑: DiagnosticBuilder 是 RAII —— 析构时才把诊断"提交"。
     * 上一个告警还在作用域里 (未提交) 就发下一条会触发
     * "Multiple diagnostics in flight at once!" 断言崩溃。
     * 所以每条诊断用 { } 限制生命周期。 */
    void report() {
        DiagnosticsEngine &diag = ctx.getDiagnostics();
        unsigned warn_id = diag.getCustomDiagID(
            DiagnosticsEngine::Warning, "unused variable '%0'");

        for (VarDecl *VD : decls) {
            if (used.count(VD->getCanonicalDecl())) continue;
            {
                auto D = diag.Report(VD->getLocation(), warn_id);
                D << VD->getNameAsString();
            }
        }
    }

private:
    ASTContext &ctx;
    std::vector<VarDecl *> decls;
    std::set<const VarDecl *> used;
};

class UnusedVarsConsumer : public ASTConsumer {
public:
    void HandleTranslationUnit(ASTContext &ctx) override {
        UnusedVarsVisitor visitor(ctx);
        visitor.TraverseTranslationUnitDecl(ctx.getTranslationUnitDecl());
        visitor.report();
    }
};

class UnusedVarsPluginAction : public PluginASTAction {
public:
    std::unique_ptr<ASTConsumer>
    CreateASTConsumer(CompilerInstance &, StringRef) override {
        return std::make_unique<UnusedVarsConsumer>();
    }

    /* 接受 -plugin-arg-unused-vars-... 形式的参数 (这里全部忽略) */
    bool ParseArgs(const CompilerInstance &,
                   const std::vector<std::string> &) override {
        return true;
    }

    /* 加载即在主动作之前运行: 不需要 -Xclang -plugin 显式选择 */
    ActionType getActionType() override { return AddBeforeMainAction; }
};

} // namespace

/* 注册: 第一个参数是插件名, 第二个是简述 (-fplugin 加载时可见) */
static FrontendPluginRegistry::Add<UnusedVarsPluginAction>
    X("unused-vars", "warn on local variables that are never referenced");
