/*
 * constfold —— 常量折叠 Pass 插件（第 11 章实战）
 *
 * 新 Pass Manager 插件: 编译成 .so, 用 opt 加载:
 *   opt-18 -load-pass-plugin=build/constfold.so \
 *          -passes=c1c-constfold fib.ll -S -o fib.folded.ll
 *
 * 做的事: 把 "两个常量操作数的运算" 在编译期算掉:
 *   %t = add i32 2, 3        →  后续都用 5, 这条指令删除
 *   %c = icmp slt i32 2, 3   →  true
 * 折叠掉一条, 上游又可能变常量 (例: (2+3)*4 的 2+3 折成 5 后,
 * 5*4 也能折), 所以循环到不动点。
 */

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/Casting.h>

using namespace llvm;

namespace {

class ConstFoldPass : public PassInfoMixin<ConstFoldPass> {
public:
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        bool changed = false;
        bool again = true;

        /* 到不动点: 一次扫描折叠掉一层, 上游可能露出新的常量运算 */
        while (again) {
            again = false;
            for (BasicBlock &BB : F) {
                for (auto it = BB.begin(); it != BB.end(); ) {
                    Instruction *I = &*it++;

                    /* 1) 常量运算: add/sub/mul/sdiv */
                    if (auto *bo = dyn_cast<BinaryOperator>(I)) {
                        if (auto *C = foldBinary(bo)) {
                            bo->replaceAllUsesWith(C);
                            bo->eraseFromParent();
                            changed = again = true;
                        }
                        continue;
                    }
                    /* 2) 常量比较: icmp */
                    if (auto *icmp = dyn_cast<ICmpInst>(I)) {
                        if (auto *C = foldICmp(icmp)) {
                            icmp->replaceAllUsesWith(C);
                            icmp->eraseFromParent();
                            changed = again = true;
                        }
                    }
                }
            }
        }
        return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }

private:
    static Constant *foldBinary(BinaryOperator *bo) {
        auto *L = dyn_cast<ConstantInt>(bo->getOperand(0));
        auto *R = dyn_cast<ConstantInt>(bo->getOperand(1));
        if (!L || !R) return nullptr;

        const APInt &a = L->getValue(), &b = R->getValue();
        switch (bo->getOpcode()) {
        case Instruction::Add: return ConstantInt::get(bo->getType(), a + b);
        case Instruction::Sub: return ConstantInt::get(bo->getType(), a - b);
        case Instruction::Mul: return ConstantInt::get(bo->getType(), a * b);
        case Instruction::SDiv:
            if (b.isZero()) return nullptr;          /* 除零留给运行时报错 */
            return ConstantInt::get(bo->getType(), a.sdiv(b));
        default:
            return nullptr;                          /* 教学版只折整数四则 */
        }
    }

    static Constant *foldICmp(ICmpInst *icmp) {
        auto *L = dyn_cast<ConstantInt>(icmp->getOperand(0));
        auto *R = dyn_cast<ConstantInt>(icmp->getOperand(1));
        if (!L || !R) return nullptr;

        const APInt &a = L->getValue(), &b = R->getValue();
        bool v;
        switch (icmp->getPredicate()) {
        case CmpInst::ICMP_SLT: v = a.slt(b); break;
        case CmpInst::ICMP_SLE: v = a.sle(b); break;
        case CmpInst::ICMP_SGT: v = a.sgt(b); break;
        case CmpInst::ICMP_SGE: v = a.sge(b); break;
        case CmpInst::ICMP_EQ:  v = a == b;  break;
        case CmpInst::ICMP_NE:  v = a != b;  break;
        default: return nullptr;
        }
        return ConstantInt::get(Type::getInt1Ty(icmp->getContext()), v);
    }
};

} // namespace

/* Pass 插件入口: opt 通过这个函数拿到插件信息 */
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "c1c-constfold", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                /* 注册成流水线语法里的 "c1c-constfold" */
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "c1c-constfold") {
                            FPM.addPass(ConstFoldPass());
                            return true;
                        }
                        return false;
                    });
                PB.registerPipelineStartEPCallback(
                    [](ModulePassManager &MPM, OptimizationLevel) {
                        MPM.addPass(
                            createModuleToFunctionPassAdaptor(ConstFoldPass()));
                    });
            }};
}
