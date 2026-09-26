/*
 * LLVM C++ API codegen —— 第 9-10 章核心实现
 *
 * 与 src/codegen/codegen.c (文本 IR 教学版) 一一对应：
 *   文本版 printf 打印什么，这里就调 IRBuilder 造什么。
 * 区别在于：IRBuilder 保证 IR 语法合法（类型/名字/终结指令自动管理），
 * 并且可以直接驱动 LLVM 后端产出 .o —— 这就是"真 codegen"。
 *
 * 编译: make c1c_full   (需要 llvm-18-dev)
 */

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

extern "C" {
#include "ast.h"
#include "tokens.h"
#include "llvm_codegen.h"
}

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>

using namespace llvm;

/* ---------- 与文本版相同的 token 兼容工具 ---------- */
static bool tok_is(int op, int tk, int hw_tk) { return op == tk || op == hw_tk; }

/* ---------- 变量绑定（对应文本版的 Binding 表） ---------- */
struct VarInfo {
    AllocaInst *slot;    /* 标量/数组的 alloca 槽 */
    GlobalVariable *gslot; /* 全局变量槽（二选一） */
    bool is_array;       /* [N x i32]，槽本身是首地址 */
    bool is_arr_param;   /* 槽里存的是调用者传来的指针 */
};

class C1CodeGen {
public:
    C1CodeGen() : B(C) {
        M = std::make_unique<Module>("c1c", C);
    }

    void run(ASTNode *prog);

    std::unique_ptr<Module> takeModule() { return std::move(M); }

private:
    LLVMContext C;
    std::unique_ptr<Module> M;
    IRBuilder<> B;

    std::map<std::string, Function *> funcs;      /* 函数名 → Function* */
    std::map<std::string, VarInfo> locals;
    std::map<std::string, VarInfo> globals;

    bool cur_fun_is_void = false;

    VarInfo *lookup(const std::string &name) {
        auto it = locals.find(name);
        if (it != locals.end()) return &it->second;
        it = globals.find(name);
        if (it != globals.end()) return &it->second;
        return nullptr;
    }

    /* ---------- 类型 ---------- */
    Type *i32()  { return Type::getInt32Ty(C); }
    Type *i1()   { return Type::getInt1Ty(C); }
    Type *ptr()  { return PointerType::get(C, 0); }   /* opaque pointer */

    /* ---------- 表达式/语句 ---------- */
    Value *gen_expr(ASTNode *e);
    Value *to_cond(Value *v);                       /* i32 → i1 */
    Value *gen_logical(ASTNode *e, bool is_and);
    Value *gen_addr(ASTNode *var);                  /* var[index] 的元素地址 */
    void   gen_stmt(ASTNode *s);
    void   gen_fun(ASTNode *d);
    void   bind_params_and_locals(ASTNode *d);
};

void C1CodeGen::run(ASTNode *prog) {
    M->setTargetTriple(sys::getDefaultTargetTriple());

    /* 预扫描: 登记函数签名（调用点需要 FunctionType） */
    for (int i = 0; i < prog->u.program.n; i++) {
        ASTNode *d = prog->u.program.decls[i];
        if (d->kind != AST_FUN_DECL) continue;

        bool is_void = (d->u.fun_decl.ret_type == T_VOID);
        std::vector<Type *> ptypes;
        for (int j = 0; j < d->u.fun_decl.n_params; j++)
            ptypes.push_back(d->u.fun_decl.params[j]->u.param.is_array
                                 ? ptr() : i32());

        FunctionType *ft = FunctionType::get(is_void ?
                                             Type::getVoidTy(C) : i32(),
                                             ptypes, false);
        Function *fn = Function::Create(ft, Function::ExternalLinkage,
                                        d->u.fun_decl.name, *M);
        funcs[d->u.fun_decl.name] = fn;
    }

    /* 全局变量 */
    for (int i = 0; i < prog->u.program.n; i++) {
        ASTNode *d = prog->u.program.decls[i];
        if (d->kind != AST_VAR_DECL) continue;

        bool is_arr = d->u.var_decl.array_size > 0;
        Type *ty = is_arr
            ? ArrayType::get(i32(), (unsigned)d->u.var_decl.array_size)
            : i32();
        auto *gv = new GlobalVariable(*M, ty, /*isConstant=*/false,
                                      GlobalValue::ExternalLinkage,
                                      Constant::getNullValue(ty),
                                      "g_" + std::string(d->u.var_decl.name));
        globals[d->u.var_decl.name] = VarInfo{nullptr, gv, is_arr, false};
    }

    /* 函数体 */
    for (int i = 0; i < prog->u.program.n; i++)
        if (prog->u.program.decls[i]->kind == AST_FUN_DECL)
            gen_fun(prog->u.program.decls[i]);
}

/* ---------- 函数 ---------- */

void C1CodeGen::gen_fun(ASTNode *d) {
    Function *fn = funcs[d->u.fun_decl.name];
    cur_fun_is_void = (d->u.fun_decl.ret_type == T_VOID);
    locals.clear();

    BasicBlock *entry = BasicBlock::Create(C, "entry", fn);
    B.SetInsertPoint(entry);

    /* 参数: 命名 %p_x, 入口 alloca + store（对应 clang -O0 的形态） */
    unsigned idx = 0;
    for (auto &arg : fn->args()) {
        ASTNode *pp = d->u.fun_decl.params[idx++];
        arg.setName("p_" + std::string(pp->u.param.name));
        bool is_arr = pp->u.param.is_array;
        AllocaInst *slot = B.CreateAlloca(is_arr ? ptr() : i32(), nullptr,
                                          pp->u.param.name + std::string(".addr"));
        B.CreateStore(&arg, slot);
        locals[pp->u.param.name] = VarInfo{slot, nullptr, is_arr, is_arr && false};
        locals[pp->u.param.name].is_arr_param = is_arr;
    }

    /* 函数体局部变量: alloca 集中在 entry */
    ASTNode *body = d->u.fun_decl.body;
    if (body && body->kind == AST_COMPOUND) {
        for (int i = 0; i < body->u.compound.nd; i++) {
            ASTNode *v = body->u.compound.decls[i];
            if (v->kind != AST_VAR_DECL) continue;
            bool is_arr = v->u.var_decl.array_size > 0;
            Type *ty = is_arr
                ? ArrayType::get(i32(), (unsigned)v->u.var_decl.array_size)
                : i32();
            AllocaInst *slot = B.CreateAlloca(ty, nullptr, v->u.var_decl.name);
            locals[v->u.var_decl.name] =
                VarInfo{slot, nullptr, is_arr, false};
        }
        for (int i = 0; i < body->u.compound.ns; i++)
            gen_stmt(body->u.compound.stmts[i]);
    }

    /* 兜底 return: 保证最后一块有终结指令 */
    if (!B.GetInsertBlock()->getTerminator()) {
        if (cur_fun_is_void) B.CreateRetVoid();
        else                 B.CreateRet(ConstantInt::get(i32(), 0));
    }
}

/* ---------- 表达式 ---------- */

Value *C1CodeGen::to_cond(Value *v) {
    if (v->getType()->isIntegerTy(1)) return v;
    return B.CreateICmpNE(v, ConstantInt::get(i32(), 0), "tobool");
}

/* var[index] 的元素地址 */
Value *C1CodeGen::gen_addr(ASTNode *var) {
    VarInfo *info = lookup(var->u.var.name);
    if (!info) {
        fprintf(stderr, "codegen: undefined variable %s (line %d)\n",
                var->u.var.name, var->line);
        return Constant::getNullValue(ptr());
    }
    Value *idx = gen_expr(var->u.var.index);
    Value *base;
    if (info->is_arr_param) {
        base = B.CreateLoad(ptr(), info->slot, "arr.ptr");
        return B.CreateGEP(i32(), base, idx, "elem.ptr");
    }
    /* 局部/全局数组: 槽指向 [N x i32], 两级 GEP */
    base = info->gslot ? cast<Value>(info->gslot) : cast<Value>(info->slot);
    Type *arr_ty = info->gslot ? info->gslot->getValueType()
                               : info->slot->getAllocatedType();
    return B.CreateGEP(arr_ty, base,
                       {ConstantInt::get(i32(), 0), idx}, "elem.ptr");
}

/* 短路求值 —— 和文本版一样用块 + phi（IRBuilder 的 CreateAnd 是
   非短路的按位与，语义不同） */
Value *C1CodeGen::gen_logical(ASTNode *e, bool is_and) {
    Function *fn = B.GetInsertBlock()->getParent();

    Value *l = to_cond(gen_expr(e->u.binop.lhs));
    BasicBlock *rhs_bb = BasicBlock::Create(
        C, is_and ? "and.rhs" : "or.rhs", fn);
    BasicBlock *end_bb = BasicBlock::Create(
        C, is_and ? "and.end" : "or.end", fn);

    BasicBlock *lhs_end = B.GetInsertBlock();   /* phi 前驱 1 */
    if (is_and) B.CreateCondBr(l, rhs_bb, end_bb);   /* 假短路 */
    else        B.CreateCondBr(l, end_bb, rhs_bb);   /* 真短路 */

    B.SetInsertPoint(rhs_bb);
    Value *r = to_cond(gen_expr(e->u.binop.rhs));
    BasicBlock *rhs_end = B.GetInsertBlock();   /* phi 前驱 2 */
    B.CreateBr(end_bb);

    B.SetInsertPoint(end_bb);
    PHINode *phi = B.CreatePHI(i1(), 2, "logic");
    phi->addIncoming(is_and ? ConstantInt::getFalse(C)
                            : ConstantInt::getTrue(C), lhs_end);
    phi->addIncoming(r, rhs_end);
    return B.CreateZExt(phi, i32(), "logic.ext");   /* C1 的逻辑值是 int */
}

Value *C1CodeGen::gen_expr(ASTNode *e) {
    if (!e) return ConstantInt::get(i32(), 0);

    switch (e->kind) {
    case AST_NUM:
        return ConstantInt::get(i32(), (uint64_t)e->u.num.value, /*isSigned=*/true);

    case AST_VAR: {
        VarInfo *info = lookup(e->u.var.name);
        if (!info) {
            fprintf(stderr, "codegen: undefined variable %s (line %d)\n",
                    e->u.var.name, e->line);
            return ConstantInt::get(i32(), 0);
        }
        if (e->u.var.index) {
            Value *p = gen_addr(e);
            return B.CreateLoad(i32(), p, "elem");
        }
        if (info->is_arr_param)                    /* 形参数组: 槽里取指针 */
            return B.CreateLoad(ptr(), info->slot, "arr.ptr");
        if (info->is_array)                        /* 数组裸用 = 首地址 */
            return info->gslot ? cast<Value>(info->gslot)
                               : cast<Value>(info->slot);
        Value *addr = info->gslot ? cast<Value>(info->gslot)
                                  : cast<Value>(info->slot);
        return B.CreateLoad(i32(), addr, e->u.var.name);
    }

    case AST_BINOP: {
        int op = e->u.binop.op;
        if (tok_is(op, TK_AND, HW_TK_AND)) return gen_logical(e, true);
        if (tok_is(op, TK_OR,  HW_TK_OR))  return gen_logical(e, false);

        Value *l = gen_expr(e->u.binop.lhs);
        Value *r = gen_expr(e->u.binop.rhs);
        if (tok_is(op, TK_PLUS,  HW_TK_PLUS))  return B.CreateAdd(l, r);
        if (tok_is(op, TK_MINUS, HW_TK_MINUS)) return B.CreateSub(l, r);
        if (tok_is(op, TK_MUL,   HW_TK_MUL))   return B.CreateMul(l, r);
        if (tok_is(op, TK_DIV,   HW_TK_DIV))   return B.CreateSDiv(l, r);
        if (tok_is(op, TK_LT,    HW_TK_LT))    return B.CreateICmpSLT(l, r);
        if (tok_is(op, TK_LE,    HW_TK_LE))    return B.CreateICmpSLE(l, r);
        if (tok_is(op, TK_GT,    HW_TK_GT))    return B.CreateICmpSGT(l, r);
        if (tok_is(op, TK_GE,    HW_TK_GE))    return B.CreateICmpSGE(l, r);
        if (tok_is(op, TK_EQ,    HW_TK_EQ))    return B.CreateICmpEQ(l, r);
        if (tok_is(op, TK_NEQ,   HW_TK_NEQ))   return B.CreateICmpNE(l, r);
        fprintf(stderr, "codegen: unknown binop %d (line %d)\n", op, e->line);
        return ConstantInt::get(i32(), 0);
    }

    case AST_ASSIGN: {
        Value *val = gen_expr(e->u.assign.expr);
        ASTNode *var = e->u.assign.var;
        if (var->u.var.index) {
            Value *p = gen_addr(var);
            B.CreateStore(val, p);
        } else {
            VarInfo *info = lookup(var->u.var.name);
            if (info) B.CreateStore(val, info->gslot ? cast<Value>(info->gslot)
                                                     : cast<Value>(info->slot));
            else fprintf(stderr, "codegen: undefined variable %s (line %d)\n",
                         var->u.var.name, var->line);
        }
        return val;                                /* 赋值表达式的值 = 右值 */
    }

    case AST_CALL: {
        Function *fn = funcs.count(e->u.call.name) ? funcs[e->u.call.name] : nullptr;
        std::vector<Value *> args;
        for (int i = 0; i < e->u.call.n; i++)
            args.push_back(gen_expr(e->u.call.args[i]));
        if (!fn) {
            fprintf(stderr, "codegen: undefined function %s (line %d)\n",
                    e->u.call.name, e->line);
            return ConstantInt::get(i32(), 0);
        }
        CallInst *call = B.CreateCall(FunctionCallee(fn->getFunctionType(), fn), args);
        if (fn->getReturnType()->isVoidTy())
            return ConstantInt::get(i32(), 0);     /* void 调用无值可给 */
        return call;
    }

    default:
        return ConstantInt::get(i32(), 0);
    }
}

/* ---------- 语句 ---------- */

/* 块已终结（ret 后的语句是死代码）——IRBuilder 能直接问当前块 */
static bool block_terminated(IRBuilder<> &b) {
    return b.GetInsertBlock()->getTerminator() != nullptr;
}

void C1CodeGen::gen_stmt(ASTNode *s) {
    if (!s || block_terminated(B)) return;

    switch (s->kind) {
    case AST_COMPOUND: {
        for (int i = 0; i < s->u.compound.nd; i++) {
            ASTNode *v = s->u.compound.decls[i];
            if (v->kind != AST_VAR_DECL) continue;
            bool is_arr = v->u.var_decl.array_size > 0;
            Type *ty = is_arr
                ? ArrayType::get(i32(), (unsigned)v->u.var_decl.array_size)
                : i32();
            AllocaInst *slot = B.CreateAlloca(ty, nullptr, v->u.var_decl.name);
            locals[v->u.var_decl.name] = VarInfo{slot, nullptr, is_arr, false};
        }
        for (int i = 0; i < s->u.compound.ns; i++)
            gen_stmt(s->u.compound.stmts[i]);
        break;
    }

    case AST_EXPR_STMT:
        gen_expr(s->u.expr_stmt.expr);
        break;

    case AST_RETURN:
        if (cur_fun_is_void || !s->u.ret_stmt.expr) {
            if (cur_fun_is_void) B.CreateRetVoid();
            else B.CreateRet(ConstantInt::get(i32(), 0));
        } else {
            B.CreateRet(gen_expr(s->u.ret_stmt.expr));
        }
        break;

    case AST_IF: {
        Function *fn = B.GetInsertBlock()->getParent();
        Value *cond = to_cond(gen_expr(s->u.if_stmt.cond));
        BasicBlock *then_bb = BasicBlock::Create(C, "if.then", fn);
        BasicBlock *end_bb  = BasicBlock::Create(C, "if.end", fn);
        BasicBlock *else_bb = s->u.if_stmt.else_br
            ? BasicBlock::Create(C, "if.else", fn) : end_bb;

        B.CreateCondBr(cond, then_bb, else_bb);

        B.SetInsertPoint(then_bb);
        gen_stmt(s->u.if_stmt.then_br);
        if (!block_terminated(B)) B.CreateBr(end_bb);   /* return 过就不补 */

        if (s->u.if_stmt.else_br) {
            B.SetInsertPoint(else_bb);
            gen_stmt(s->u.if_stmt.else_br);
            if (!block_terminated(B)) B.CreateBr(end_bb);
        }
        B.SetInsertPoint(end_bb);
        break;
    }

    case AST_WHILE: {
        Function *fn = B.GetInsertBlock()->getParent();
        BasicBlock *cond_bb = BasicBlock::Create(C, "while.cond", fn);
        BasicBlock *body_bb = BasicBlock::Create(C, "while.body", fn);
        BasicBlock *end_bb  = BasicBlock::Create(C, "while.end", fn);

        if (!block_terminated(B)) B.CreateBr(cond_bb);

        B.SetInsertPoint(cond_bb);
        Value *cond = to_cond(gen_expr(s->u.while_stmt.cond));
        B.CreateCondBr(cond, body_bb, end_bb);

        B.SetInsertPoint(body_bb);
        gen_stmt(s->u.while_stmt.body);
        if (!block_terminated(B)) B.CreateBr(cond_bb);  /* 回边 */

        B.SetInsertPoint(end_bb);
        break;
    }

    default:
        break;
    }
}

/* ---------- 对外接口 ---------- */

/* 第 11 章预览: 显式列出的优化流水线 */
static void run_opt_pipeline(Module &M) {
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager MPM;
    /* mem2reg: 把 alloca/load/store 提升为 SSA 寄存器 —— 教程里
       "SSA 化"的真实名字。之后 instcombine 清废指令, simplifycfg 合并块 */
    FunctionPassManager FPM;
    FPM.addPass(PromotePass());
    FPM.addPass(InstCombinePass());
    FPM.addPass(SimplifyCFGPass());
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

    MPM.run(M, MAM);
}

extern "C" int c1_llvm_emit_text(ASTNode *root, FILE *out, int optimize) {
    C1CodeGen CG;
    CG.run(root);
    std::unique_ptr<Module> M = CG.takeModule();
    if (optimize) run_opt_pipeline(*M);

    std::string verr;
    raw_string_ostream ves(verr);
    if (verifyModule(*M, &ves)) {
        fprintf(stderr, "IR verification failed:\n%s\n", ves.str().c_str());
        return 1;
    }

    std::string buf;
    raw_string_ostream os(buf);
    M->print(os, nullptr);
    fwrite(buf.data(), 1, buf.size(), out);
    return 0;
}

extern "C" int c1_llvm_emit_object(ASTNode *root, const char *obj_path, int optimize) {
    C1CodeGen CG;
    CG.run(root);
    std::unique_ptr<Module> M = CG.takeModule();
    if (optimize) run_opt_pipeline(*M);

    std::string verr;
    raw_string_ostream ves(verr);
    if (verifyModule(*M, &ves)) {
        fprintf(stderr, "IR verification failed:\n%s\n", ves.str().c_str());
        return 1;
    }

    /* 第 10 章: 驱动 LLVM 后端, IR → 目标代码 */
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    std::string triple = sys::getDefaultTargetTriple();
    std::string err;
    const Target *target = TargetRegistry::lookupTarget(triple, err);
    if (!target) {
        fprintf(stderr, "target not found: %s\n", err.c_str());
        return 1;
    }

    TargetOptions opts;
    TargetMachine *TM = target->createTargetMachine(
        triple, "generic", "", opts, std::nullopt);
    if (!TM) {
        fprintf(stderr, "cannot create TargetMachine\n");
        return 1;
    }
    M->setDataLayout(TM->createDataLayout());
    M->setTargetTriple(triple);

    std::error_code EC;
    raw_fd_ostream dest(obj_path, EC, sys::fs::OF_None);
    if (EC) {
        fprintf(stderr, "cannot open %s: %s\n", obj_path,
                EC.message().c_str());
        return 1;
    }

    legacy::PassManager emit;
    if (TM->addPassesToEmitFile(emit, dest, nullptr,
                                CodeGenFileType::ObjectFile)) {
        fprintf(stderr, "cannot emit object file\n");
        return 1;
    }
    emit.run(*M);
    dest.flush();
    return 0;
}
