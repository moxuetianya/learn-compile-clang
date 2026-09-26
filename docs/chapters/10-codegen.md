# 第 10 章：代码生成与运行时

> 第 9 章我们能生成 LLVM IR 文本，本章我们把它变成可执行文件；讲清楚函数如何被调用、局部变量如何布局、数组如何寻址。

## 10.1 整条管线

```
.c1 ──c1c──► .ll (LLVM IR)
              │
              ▼  llc -filetype=obj
              .o (目标文件)
              │
              ▼  clang/lld 链接（crt0、运行时）
              可执行文件
```

或者更直接：

```bash
clang -O0 fib.ll -o fib     # 用 clang 当 assembler+linker
```

## 10.2 ABI 基础：调用约定

C1 函数用 C 调用约定（System V AMD64）：

| 位置/寄存器 | 类型 | 备注 |
| ----------- | ---- | ---- |
| `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` | 整型/指针前 6 个参数 | 顺序固定 |
| `xmm0..xmm7` | 浮点前 8 个参数 | C1 不用 |
| `rax` | 返回值 | 整数/指针 |
| `rbx`, `rbp`, `r12-r15` | 被调者保存 | 用到就要 push/pop |
| `rsp` | 栈顶 | |

LLVM IR **不暴露这些寄存器**——它指定参数和返回值类型，llc 之后端会自动选择寄存器。所以从 IR 写起是位置无关的。

## 10.3 函数调用：递归的 fib

```c
int fib(int n) {
    if (n < 2) return n;
    return fib(n-1) + fib(n-2);
}
```

翻译：

```llvm
define i32 @fib(i32 %n) {
entry:
  %tmp = icmp slt i32 %n, 2
  br i1 %tmp, label %then, label %else

then:
  ret i32 %n

else:
  %t1 = sub i32 %n, 1
  %r1 = call i32 @fib(i32 %t1)
  %t2 = sub i32 %n, 2
  %r2 = call i32 @fib(i32 %t2)
  %r3 = add i32 %r1, %r2
  ret i32 %r3
}
```

生成的汇编（llc 输出）：

```
fib:
    cmpl    $2, %edi
    jge     .LBB0_2
    movl    %edi, %eax
    ret

.LBB0_2:
    leal    -1(%rdi), %eax
    movl    %eax, %edi
    pushq   %rbx
    movl    %edi, %ebx
    callq   fib
    movl    %eax, %ecx
    leal    -2(%rbx), %edi
    callq   fib
    addl    %ecx, %eax
    popq    %rbx
    ret
```

> 注意 `pushq %rbx` / `popq %rbx`：因为 `ebx` 是被调者保存寄存器，递归调用 fib 前必须保护。

## 10.4 局部变量布局

C1 局部变量翻译为 `alloca`：

```llvm
define i32 @f() {
entry:
  %x = alloca i32           ; 4 字节
  %a = alloca [10 x i32]    ; 40 字节
  store i32 5, i32* %x
  %v = load i32, i32* %x
  ret i32 %v
}
```

`alloca` 在函数栈帧上分配空间。LLVM 的 `alloca` 是 **按需** 的，所以
```llvm
%x = alloca i32
store i32 5, i32* %x
%v = load i32, i32* %x
```

在优化后会被合并为：

```
%v = i32 5
```

## 10.5 数组

`int a[10];` → `[10 x i32]*`
`a[i]` → `getelementptr + load`

```llvm
%idx = ...                        ; i
%ptr = getelementptr [10 x i32], [10 x i32]* %a, i32 0, i32 %idx
%v   = load i32, i32* %ptr
```

**`getelementptr` 不读不写内存**，它只算地址。"0" 是"跳到第一个元素"。这是 GEP 的常见疑惑点。

C1 数组作为函数参数：`int a[]` 实际是 `int*`：

```c
void f(int a[]) { a[0] = 1; }
```

翻译：

```llvm
define void @f(i32* %a) {
  %ptr = getelementptr i32, i32* %a, i32 0
  store i32 1, i32* %ptr
  ret void
}
```

> C1 不检查下标越界（与 ref 文档一致）。Clang 默认也不检查，要 `-fsanitize=address` 才查。

## 10.6 全局变量

```llvm
@g = global i32 0

define void @inc() {
  %t = load i32, i32* @g
  %t1 = add i32 %t, 1
  store i32 %t1, i32* @g
  ret void
}
```

注意全局用 `@`，局部 alloca 用 `%`。

## 10.7 main 函数

C1 要求最后一个声明是 `int main(void)`，但写不写 `return expr;` 都行（漏写时我们补 `return 0;`）。

```cpp
// 在 codegen 最后
if (current_function_is_main && !bb_has_terminator) {
    builder.CreateRet(builder.getInt32(0));
}
```

## 10.8 运行时支持

C1 的 "运行时" 几乎为零：
- `_start` crt0：准备 argc/argv、调用 main、退出
- `printf`：标准库
- 没有 GC、没有异常、没有线程局部存储

所以链接时只需要：

```bash
clang fib.ll -o fib          # 自动链接 libc 和 crt0
```

或显式：

```bash
llc fib.ll -filetype=obj -o fib.o
clang fib.o -o fib -no-pie
```

## 10.9 完整流程脚本

`scripts/build_and_run.sh`：

```bash
#!/usr/bin/env bash
set -e
SRC=${1:-examples/fib.c1}
LLVM_CONFIG=$(which llvm-config-18 || which llvm-config)

# 前端：c1c → .ll
./build/c1c "$SRC" -o build/out.ll

# 中端：llc → .s
llc-18 build/out.ll -o build/out.s

# 后端：assembler + linker
clang-18 build/out.s -o build/out

# 跑
./build/out
echo "exit=$?"
```

## 10.10 性能调优入门

C1 子集跑得很快，但有几个明显的优化点：

| 优化 | 说明 |
| ---- | ---- |
| `mem2reg` | 把 alloca 提升到 SSA 寄存器 —— LLVM 默认开了 |
| `instcombine` | `x + 0` 之类化简 |
| `inline` | 小函数内联 |
| `loop-unroll` | 展开小循环 |

```bash
# 看优化前后的 IR
llc fib.ll -O0 -o /tmp/o0.s
llc fib.ll -O2 -o /tmp/o2.s
diff /tmp/o0.s /tmp/o2.s | less
```

## 10.11 常见错误

1. **alloca 在循环里**：性能差（每次循环分配栈空间）
   ```llvm
   bad:
   while.cond:
     %x = alloca i32   ; ← 错
   ```
2. **终结符忘了**：BasicBlock 没有 ret/br → verifier error
3. **alloca 的 Type 和 store 的类型不一致**：segfault
4. **getelementptr 索引写错**：常见的是 `[N x T]*` 的 GEP 需要两个索引
5. **global initializer 不是 constant**：llvm 不接受运行时初始化 global

## 10.12 现场常用方法的优劣

| 后端策略 | 代表 | 优劣 |
| -------- | ---- | ---- |
| LLVM IR → llc | Clang、Rust、Swift | 后端跨平台，调试工具丰富 |
| Cranelift | 部分 Rust nightly、WebAssembly | 编译快，但优化弱 |
| 自写后端 | TCC、某些嵌入式 | 完全控制体积/速度 |
| Interpreter | CPython、Ruby MRI | 启动快但慢 |

> 现代编译器后端几乎都是 LLVM 或 Cranelift；"自己写指令选择 + 寄存器分配"只有研究或特殊领域（GPU、DSP）才有意义。

## 10.13 小结

- C1 编译器后端 = LLVM IR → llc → 汇编 → 链接
- ABI 由 LLVM 处理，我们只写 SSA
- 数组、指针、全局都用 LLVM 对应原语
- main 函数需要补 `ret 0` 处理

**练习**：
1. 写一个 `swap(int *a, int *b)` 的 C1 程序并编译运行。
2. 写一个返回数组最大值的函数 `max(int a[], int n)`。
3. 用 `llc -O0` vs `-O2` 看 fib.s 的差别；理解为什么优化重要。

---
**下一章**：[第 11 章 优化入门](11-optimization.md)
