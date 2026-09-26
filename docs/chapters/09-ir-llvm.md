# 第 09 章：中间表示与 LLVM IR

> 语法和语义都对了，下一步是把 AST 翻译成 IR。本章我们用 **LLVM IR** 作为目标 IR，先手写 `.ll` 文本，理解 IR 的本质；再引入 `llvm::IRBuilder`，用 C++ API 直接构造。

## 9.1 为什么要 IR

直接 AST → 汇编难在哪？
- AST 不够底层（变量名、表达式嵌套、控制流都太"语言级"）
- 不同 CPU 指令差异巨大（x86 vs ARM vs RISC-V）
- 优化在 AST 上难做（结构对优化不友好）

**IR = 一种既贴近源语言语义，又容易优化的低层表示。**

### 9.1.1 三地址码 / SSA

经典的 IR 是 **三地址码**（TAC）：

```
t1 = 2 + 3
t2 = t1 * 4
x  = t2
```

每条指令最多 3 个操作数。

**SSA**（Static Single Assignment）= 每个变量只赋值一次。

```
%1 = add i32 2, 3
%2 = mul i32 %1, 4
store i32 %2, i32* %x
```

SSA 的优势：每个值有唯一的"出生点"，数据流分析极其方便。
**LLVM IR 就是 SSA 形式的**。

## 9.2 LLVM IR 速成

```llvm
; 外部函数声明
declare i32 @printf(i8*, ...)

; 全局变量
@x = global i32 0

; 函数定义
define i32 @main() {
entry:
    %1 = add i32 2, 3
    %2 = mul i32 %1, 4
    ret i32 %2
}
```

要点：
- `i32`、`i64`、`float`、`double`：基本类型
- `%x`：SSA 局部值（虚拟寄存器）
- `@x`：全局标识符
- `ptr`：指针类型。LLVM 15+ 使用 **opaque pointer**（所有指针统一写成 `ptr`，不再带 `i32*`/`i8**` 这种点类型）；旧式写法 `i32*` 仍能解析，等价于 `ptr`
- `getelementptr`、`load`、`store`：内存操作
- `br`、`switch`：控制流
- `ret`：函数返回
- `declare` vs `define`：声明 / 定义

## 9.3 第一步：手写 IR 输出（无 LLVM API）

> 本章实现：[`src/codegen/codegen.c`](../../src/codegen/codegen.c)（文本版）+ 
> [`src/driver/main_emit.c`](../../src/driver/main_emit.c)。
> `make c1c_emit && ./build/c1c_emit < examples/fib.c1` 即可复现。

不依赖 LLVM，直接打印出 IR 文本，能加深理解。

```c
const char *gen_expr(ASTNode *e) {
    switch (e->kind) {
    case AST_NUM:
        return const_name(e->u.num.value);   /* 直接用字面量 "42" */
    case AST_BINOP: {
        const char *lhs = gen_expr(e->u.binop.lhs);
        const char *rhs = gen_expr(e->u.binop.rhs);
        const char *r = fresh();             /* 新临时 "%t3" */
        printf("  %s = %s i32 %s, %s\n", r, op, lhs, rhs);
        return r;
    }
    /* ... */
    }
}
```

特点：
- 每个表达式返回一个**操作数字符串**：要么是 `%tN`（SSA 临时），要么是字面量
- 常量直接用字面量，**不要**生成 `%t1 = add i32 5, 0` 这种废指令
- 表达式是递归翻译的

```c
int main(int argc, char **argv) {
    yyparse();   /* 已建立 AST: program_root */
    sema_check(ctx, program_root);   /* 语义错误不进后端 */
    gen_program(program_root);
    return 0;
}
```

输出 `fib.ll`，验证 + 编译 + 运行：

```bash
./build/c1c_emit < examples/fib.c1 > fib.ll
opt-18 -passes=verify -disable-output fib.ll   # IR 验证器
llc-18 fib.ll -o fib.s
clang-18 fib.s -o fib
./fib ; echo $?    # 55
```

### 本实现踩过的坑（真实事故记录）

1. **ret 后面又发 br**：`if (n<2) return n;` 的 then 块已有 `ret`，
   再补 `br label %end` 就是"一个块两条终结指令"——verifier 直接拒绝。
   解法：用 `terminated` 标志跟踪当前块状态，已终结就不再发 br/ret。
2. **数组读值返回了指针**：`a[i]` 的 GEP 结果是地址，忘了再 `load`。
3. **`&&`/`||` 不是普通 binop**：短路求值需要拆基本块 + `phi` 汇合
   （见 9.5.8）。
4. **条件类型**：`if (x)` 里 x 是 i32，`br i1` 需要 i1 —— 补
   `%t = icmp ne i32 %x, 0`。
5. **数组元素寻址**：局部数组槽指向 `[N x i32]`，要两级 GEP
   （先 0 跳过数组本身，再走下标）；数组参数槽里存的是指针，先 load 再一级 GEP。

## 9.4 第二步：用 LLVM C++ API

手写 IR 适合教学，工业代码必须用 API（避免字符串拼接、方便做分析）。

### 9.4.1 引入 LLVM

CMake 片段：

```cmake
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(c1c src/main.cpp src/ast.cpp src/sema.cpp src/codegen.cpp)
target_link_libraries(c1c ${LLVM_LIBRARIES})
```

### 9.4.2 最小 LLVM 程序

```cpp
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>

int main() {
    llvm::LLVMContext ctx;
    llvm::Module *mod = new llvm::Module("c1c", ctx);
    llvm::IRBuilder<> builder(ctx);

    // 函数: int add(int a, int b) { return a + b; }
    llvm::FunctionType *fty = llvm::FunctionType::get(
        builder.getInt32Ty(), {builder.getInt32Ty(), builder.getInt32Ty()}, false);
    llvm::Function *f = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
                                               "add", mod);
    llvm::BasicBlock *bb = llvm::BasicBlock::Create(ctx, "entry", f);
    builder.SetInsertPoint(bb);
    llvm::Value *a = f->getArg(0), *b = f->getArg(1);
    llvm::Value *r = builder.CreateAdd(a, b);
    builder.CreateRet(r);

    mod->print(llvm::errs(), nullptr);   // 打印 IR
    llvm::verifyModule(*mod);             // 验证
    return 0;
}
```

输出：

```
; ModuleID = 'c1c'
source_filename = "c1c"

define i32 @add(i32 %0, i32 %1) {
entry:
  %3 = add i32 %0, %1
  ret i32 %3
}
```

## 9.5 从 AST 到 LLVM IR

### 9.5.1 翻译模式

每个 AST 节点 = 一个 IR 生成函数，**返回值**是 `llvm::Value*`（这个节点代表的 SSA 值）。

```cpp
llvm::Value* cg_expr(ASTNode *e);
```

### 9.5.2 整数运算

```cpp
case AST_BINOP: {
    llvm::Value *L = cg_expr(e->u.binop.lhs);
    llvm::Value *R = cg_expr(e->u.binop.rhs);
    switch (e->u.binop.op) {
    case T_ADD: return builder.CreateAdd(L, R);
    case T_SUB: return builder.CreateSub(L, R);
    case T_MUL: return builder.CreateMul(L, R);
    case T_DIV: return builder.CreateSDiv(L, R);
    case T_LT:  return builder.CreateICmpSLT(L, R);
    case T_GT:  return builder.CreateICmpSGT(L, R);
    case T_LE:  return builder.CreateICmpSLE(L, R);
    case T_GE:  return builder.CreateICmpSGE(L, R);
    case T_EQ:  return builder.CreateICmpEQ(L, R);
    case T_NEQ: return builder.CreateICmpNE(L, R);
    }
}
```

### 9.5.3 局部变量

C1 局部变量翻译成 LLVM `alloca` + `store`/`load`：

```cpp
case AST_VAR_DECL: {
    llvm::Type *t = to_llvm_type(e->u.var_decl.type);
    llvm::AllocaInst *slot = builder.CreateAlloca(t, nullptr,
                                                  e->u.var_decl.name);
    named_values[e->u.var_decl.name] = slot;
    return nullptr;
}
```

引用变量：

```cpp
case AST_VAR: {
    llvm::AllocaInst *slot = named_values[e->u.var.name];
    if (e->u.var.index) {     // 数组下标
        llvm::Value *idx = cg_expr(e->u.var.index);
        return builder.CreateLoad(t, builder.CreateGEP(t, slot, idx));
    }
    return builder.CreateLoad(slot->getAllocatedType(), slot, e->u.var.name);
}
```

> `named_values` 是当前函数作用域的"名字 → 栈槽"映射。

### 9.5.4 赋值

```cpp
case AST_ASSIGN: {
    llvm::Value *v = cg_expr(e->u.assign.var);  // 算左值地址
    llvm::Value *r = cg_expr(e->u.assign.expr);
    builder.CreateStore(r, v);
    return r;  // C 语言 a = b 的值是 a 的新值
}
```

### 9.5.5 控制流

`if` 翻译成 3 个基本块：

```cpp
case AST_IF: {
    llvm::Value *cond = cg_expr(e->u.if_stmt.cond);
    llvm::Function *fn = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *then_bb = llvm::BasicBlock::Create(ctx, "then", fn);
    llvm::BasicBlock *else_bb = e->u.if_stmt.else_br
        ? llvm::BasicBlock::Create(ctx, "else", fn) : nullptr;
    llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(ctx, "ifend", fn);

    builder.CreateCondBr(cond, then_bb, else_bb ? else_bb : merge_bb);

    // then 分支
    builder.SetInsertPoint(then_bb);
    cg_stmt(e->u.if_stmt.then_br);
    if (!then_bb->getTerminator())          // 提前 return 过就不补 br
        builder.CreateBr(merge_bb);

    // else 分支
    if (else_bb) {
        builder.SetInsertPoint(else_bb);
        cg_stmt(e->u.if_stmt.else_br);
        if (!else_bb->getTerminator())
            builder.CreateBr(merge_bb);
    }

    builder.SetInsertPoint(merge_bb);
    return nullptr;
}
```

> **坑**：如果 then 分支里提前 ret，再无条件 `CreateBr(merge_bb)` 会造成
> "一个块两条终结指令"→ verifier 报错。上面的代码在补 br 前检查了
> `getTerminator()`。更省心的做法是封装一个 `br_to(target)`，内部做这个检查
> （仓库实现就是这么写的）。

### 9.5.6 while 循环

```cpp
case AST_WHILE: {
    llvm::Function *fn = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *cond_bb = llvm::BasicBlock::Create(ctx, "while.cond", fn);
    llvm::BasicBlock *body_bb = llvm::BasicBlock::Create(ctx, "while.body", fn);
    llvm::BasicBlock *end_bb  = llvm::BasicBlock::Create(ctx, "while.end", fn);
    builder.CreateBr(cond_bb);

    builder.SetInsertPoint(cond_bb);
    llvm::Value *cond = cg_expr(e->u.while_stmt.cond);
    builder.CreateCondBr(cond, body_bb, end_bb);

    builder.SetInsertPoint(body_bb);
    cg_stmt(e->u.while_stmt.body);
    builder.CreateBr(cond_bb);

    builder.SetInsertPoint(end_bb);
    return nullptr;
}
```

### 9.5.7 短路求值：&& 与 ||

`a && b` 的语义是"先算 a，假则短路"，不能翻译成一条 `and` 指令，
必须拆块，用 **phi 节点**把两条路径的值汇合：

```llvm
  ; a && b
  %ac = icmp ne i32 %a, 0          ; 左边转成 i1
  br i1 %ac, label %and.rhs, label %and.end   ; 假 → 短路
and.rhs:
  %bc = icmp ne i32 %b, 0
  br label %and.end
and.end:
  %r = phi i1 [ 0, %entry ], [ %bc, %and.rhs ]  ; 哪条路径来的？
  %v = zext i1 %r to i32           ; C1 的逻辑值是 int
```

phi 的语义：**从前驱块 X 跳过来时，取 X 对应的值**。这是 SSA 处理
"同一个变量多处赋值"的标准答案（循环归纳变量也是这么消解的）。

`a || b` 对称：真短路，phi 的短路值是 1。

> 仓库实现：`gen_logical()`（文本版 `codegen.c` / API 版
> `llvm_codegen.cpp`），两边逐条对应。

### 9.5.8 函数

```cpp
void cg_function(ASTNode *f) {
    llvm::FunctionType *ft = ...;
    llvm::Function *fn = llvm::Function::Create(ft, ...);
    llvm::BasicBlock *bb = llvm::BasicBlock::Create(ctx, "entry", fn);
    builder.SetInsertPoint(bb);

    named_values.clear();
    for (auto &arg : fn->args()) {
        llvm::AllocaInst *slot = builder.CreateAlloca(arg.getType(), nullptr);
        builder.CreateStore(&arg, slot);
        named_values[arg.getName()] = slot;
    }
    cg_stmt(f->u.fun_decl.body);
    // 默认 ret 0 for main
    if (!bb->back().isTerminator())
        builder.CreateRet(builder.getInt32(0));
}
```

## 9.6 关键 LLVM API 速查

| C++ API | 生成的 IR |
| ------- | --------- |
| `IRBuilder::CreateAdd(L, R)` | `%r = add i32 %L, %R` |
| `IRBuilder::CreateAlloca(T, n, name)` | `%name = alloca T` |
| `IRBuilder::CreateLoad(T, p)` | `%r = load T, ptr %p` |
| `IRBuilder::CreateStore(V, p)` | `store T %V, ptr %p` |
| `IRBuilder::CreateGEP(T, p, idx)` | `%r = getelementptr T, ptr %p, i32 %idx` |
| `IRBuilder::CreateBr(bb)` | `br label %bb` |
| `IRBuilder::CreateCondBr(c, t, f)` | `br i1 %c, label %t, label %f` |
| `IRBuilder::CreateRet(V)` | `ret T %V` |
| `IRBuilder::CreateICmpEQ(L, R)` | `%r = icmp eq i32 %L, %R` |
| `IRBuilder::CreatePHI(T, n)` | `%r = phi T [ v1, %bb1 ], [ v2, %bb2 ]` |

## 9.7 输出与运行

```cpp
// 文本格式
std::error_code ec;
llvm::raw_fd_ostream out("fib.ll", ec);
mod->print(out, nullptr);

// 或二进制格式（更紧凑）
llvm::WriteBitcodeToFile(*mod, out);

// 编译运行
// llc fib.ll -o fib.s && gcc fib.s -o fib && ./fib
```

## 9.8 现场常用的几条 IR 准则

1. **每个函数一个 entry basic block**，最后有 ret
2. **每个 BasicBlock 终止于 terminator**（br/ret/switch/unreachable）
3. **所有 SSA 值在使用前定义**
4. **不在 loop 里 alloca**（性能考虑；放函数开头）
5. **优先用 `builder.Create*` 而非手写字符串**

## 9.9 实战：跑通仓库里的两条路径

```bash
# 路径 A: 文本 IR 教学版
$ ./build/c1c_emit < examples/fib.c1
define i32 @fib(i32 %p_n) {
entry:
  %t1 = alloca i32
  store i32 %p_n, ptr %t1
  %t2 = load i32, ptr %t1
  %t3 = icmp slt i32 %t2, 2
  br i1 %t3, label %then0, label %if.end0
then0:
  %t4 = load i32, ptr %t1
  ret i32 %t4
if.end0:
  ...
}

# 路径 B: LLVM C++ API 版（一条龙到可执行文件）
$ ./build/c1c_full examples/fib.c1 -o fib && ./fib ; echo $?
55
$ ./build/c1c_full examples/fib.c1 --emit-llvm --opt   # 优化后的 IR
```

`--opt` 跑 mem2reg + instcombine + simplifycfg：alloca/load/store 全部
消失，值直接用 SSA 寄存器和 `%p_n`——这就是"SSA 化"的真身，也是
clang -O0 IR 与 -O2 IR 的本质区别之一。对比前后 IR 是本章最好的练习。

## 9.10 小结

- IR 是连接前端和后端的桥梁，LLVM IR 是 SSA 形式
- 翻译 AST 时每个表达式返回一个 `llvm::Value*`
- 控制流翻译成 BasicBlock 链
- LLVM C++ API 比手写字符串更安全

**练习**：
1. 用 LLVM API 写一个 `int add(int a, int b)` 函数，dump IR。
2. 在 codegen 里加调试输出：打印每个 AST 节点翻译时生成的 SSA 名字。
3. 处理"函数最后没有 return"的警告 —— 补一个 `ret 0`。

---
**下一章**：[第 10 章 代码生成与运行时](10-codegen.md) —— 把 IR 链接成可执行文件。
