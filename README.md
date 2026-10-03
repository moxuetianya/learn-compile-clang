# 编译原理实战教程 —— 从 flex/bison 到 LLVM

> 面向计算机专业本科/研究生的编译原理学习教程。
> 主线：用 C/C++ 实现一个 **C1 语言子集** 的编译器，前端使用 `flex`+`bison`（含手写递归下降作为对照），
> 后端使用 **LLVM IR** 生成可执行文件。

---

## 学习路线图

```
词法 ──→ 语法 ──→ 语义 ──→ 中间表示 ──→ 代码生成 ──→ 优化与目标代码
Lex       Yacc/     作用域/    AST +         LLVM IR     llc → 原生
          手写RD    类型检查   SSA化
```

每个阶段都有两样东西：**理论讲解**（讲清楚"是什么"和"为什么这样设计"）+ **实战代码**（边学边写）。

---

## 章节大纲

| #  | 章节 | 主题 | 实践 |
| -- | ---- | ---- | ---- |
| 0  | [概览](docs/chapters/00-overview.md) | 编译器全貌、为什么要学、前置知识 | 跑通一个最简单的 `clang` 命令 |
| 1  | [词法分析：理论](docs/chapters/01-lex-theory.md) | 正则语言、有穷自动机、DFA/NFA、词法分析器的手写方法 | 词法分析器家族族谱：手写 vs 表格驱动 vs 工具生成 |
| 2  | [词法分析：flex 实践](docs/chapters/02-lex-flex.md) | flex 语法、状态、yylval、错误恢复 | 用 flex 实现 C1 词法分析器 |
| 3  | [语法分析：理论](docs/chapters/03-parse-theory.md) | CFG、推导、歧义、左递归、LL/LR 比较 | 文法改写练习：消除左递归、提取左公因子 |
| 4  | [自顶向下：递归下降与 LL(1)](docs/chapters/04-top-down.md) | FIRST/FOLLOW 集、预测分析表、递归下降 | 手写一个计算器 / 表达式的递归下降分析器 |
| 5  | [自底向上：LR(0)/SLR/LR(1)/LALR](docs/chapters/05-bottom-up.md) | 项目集、closure/goto、冲突与解决 | 用工具/手算构造 LR(0) 分析表 |
| 6  | [语法分析：bison 实践](docs/chapters/06-bison-yacc.md) | bison 语法、%token/%left/优先级、错误恢复 | 用 bison 实现 C1 语法分析器 + 抽象语法树 |
| 7  | [抽象语法树 AST](docs/chapters/07-ast.md) | AST vs 解析树、Visitor 模式、内存管理 | 用 Visitor 实现打印/AST 可视化（Graphviz） |
| 8  | [语义分析](docs/chapters/08-sema.md) | 符号表、作用域、类型系统、类型检查 | 实现 C1 的符号表 + 类型检查器 |
| 9  | [中间表示与 LLVM IR](docs/chapters/09-ir-llvm.md) | 三地址码、SSA、LLVM IR 基础 | 用 `llvm::IRBuilder` 把 AST 翻译成 LLVM IR |
| 10 | [代码生成与运行时](docs/chapters/10-codegen.md) | 栈帧、调用约定、局部变量布局 | 让 C1 函数能跑起来：`fib`/`fact`/`qsort` |
| 11 | [优化入门](docs/chapters/11-optimization.md) | 局部优化、窥孔优化、LLVM Pass 雏形 | 写一个 `constfold` Pass |
| 12 | [Clang 架构与插件开发](docs/chapters/12-clang-ast.md) | Clang 前端架构、ASTMatcher、Plugin | 写一个简单的 Clang Plugin |
| 13 | [JIT 与解释执行（可选）](docs/chapters/13-jit.md) | LLVM JIT、MCJIT、ORC | 用 ORC JIT 跑一个表达式 |
| 14 | [期末项目](docs/chapters/14-project.md) | 综合：扩展 C1、添加 `for`/结构体/字符串 | 个人项目选题 |

---

## 项目最终目标

实现一个能编译 **C1 子集** 的编译器 `c1c`：

```bash
$ ./build/c1c_full examples/fib.c1 -o fib
$ ./fib ; echo $?
55
```

能力清单（按章节递进）：
- [x] 第 2 章：词法
- [x] 第 6 章：语法
- [x] 第 8 章：作用域/类型
- [x] 第 9 章：翻译成 LLVM IR（文本版 + LLVM C++ API 版）
- [x] 第 10 章：可执行文件（进程内 TargetMachine + 系统链接器）
- [x] 第 11 章：优化流水线（`--opt`）+ 自研常量折叠 Pass 插件
- [x] 第 12 章：Clang 插件（unused-vars，`-fplugin` 加载）
- [x] 第 13 章：ORC LLJIT（`c1c_jit`，进程内执行）

---

## 工具链

| 工具 | 用途 | 安装 |
| ---- | ---- | ---- |
| `flex` | 词法分析器生成 | `sudo apt install flex` |
| `bison` | 语法分析器生成 | `sudo apt install bison` |
| `clang/clang++` | 编译我们的编译器 | `sudo apt install clang` |
| `llvm` | LLVM C++ API、IR、Pass | `sudo apt install llvm-18-dev` |
| `llc` | IR → 汇编 | `sudo apt install llvm-18` |
| `graphviz` | AST 可视化 | `sudo apt install graphviz` |

> Ubuntu 24.04 默认 llvm-18。Makefile 已自动探测版本。

---

## 目录结构

```
.
├── docs/
│   ├── chapters/        # 教程章节（markdown）
│   ├── reference/       # 参考资料整理（两份 ref 的 markdown 版）
│   └── syllabus.md      # 课程大纲
├── src/                 # 编译器源码
│   ├── lexer/           # 词法: flex 源文件 + 手写版本
│   ├── parser/          # bison 源文件
│   ├── ast/             # 抽象语法树
│   ├── sema/            # 语义分析（符号表、类型）
│   ├── codegen/         # codegen.c 文本 IR 教学版
│   │                    # + llvm_codegen.cpp LLVM C++ API 版（第 9-10 章）
│   ├── passes/          # LLVM Pass 插件: constfold（第 11 章）
│   ├── clang-plugin/    # Clang 插件: unused-vars（第 12 章）
│   └── driver/          # main、选项解析（main_full.cpp / main_jit.cpp）
├── tests/               # 测试用例与脚本
│   ├── expected/        # AST 期望输出
│   └── e2e/             # 端到端期望退出码
├── examples/            # C1 示例程序
├── scripts/             # 构建/测试脚本
└── build/               # 构建产物（git 忽略）
```

## 当前已实现

| 工具 | 状态 | 章节 | 说明 |
| ---- | ---- | ---- | ---- |
| `c1c_hand` | ✅ | 第 1-7 章 | 纯 C 手写 lexer+parser，打印 AST |
| `c1c_sema` | ✅ | 第 8 章 | 加上语义分析（符号表、类型） |
| `c1c` | ✅ | 第 2-6 章 | flex/bison 版本（需安装 flex+bison） |
| `c1c_emit` | ✅ | 第 9 章 | 文本 LLVM IR 生成（教学版，短路求值/数组/全局变量齐全） |
| `c1c_full` | ✅ | 第 9-11 章 | LLVM C++ API 完整编译器：一条龙到可执行文件；`--emit-llvm` / `--emit-obj` / `--opt`（需 llvm-18-dev） |
| `c1c_jit` | ✅ | 第 13 章 | ORC LLJIT：IR 不落盘，进程内直接执行 main（需 llvm-18-dev） |
| `constfold.so` | ✅ | 第 11 章 | 自研常量折叠 Pass 插件，`opt -load-pass-plugin` 加载，与 LLVM pass 组合流水线 |
| `unused-vars.so` | ✅ | 第 12 章 | Clang 插件：告警从未引用的局部变量，`clang -fplugin` 加载（需 libclang-cpp-18-dev） |

跑测试：

```bash
make test
```

会编译全部工具，跑三层测试：
1. **AST 比对**（手写 + flex/bison 两套前端，行为必须一致）
2. **语义检查**
3. **端到端**：每个程序经三条路径编译运行（`c1c_emit → opt 验证 → llc → clang`、
   `c1c_full` 直出、`c1c_full --opt` 优化模式），退出码必须一致

如果 flex/bison 安装在非标准位置（例如 `dpkg -x` 解压后）：

```bash
PATH=/path/to/flexbison/usr/bin:$PATH make test
```

Makefile 会自动推断 `BISON_PKGDATADIR` 让 bison 找到它的 m4 文件。

## 测试统计

```
AST parser tests:   17 passed
Semantic tests:     16 passed
E2E (×3 条路径):    14 passed   (文本 IR 流水线 / c1c_full / c1c_full --opt)
JIT (×2 种模式):    14 passed   (c1c_jit / c1c_jit --opt)
Pass 插件:           2 passed   (折到只剩 ret + 运行行为不变)
Clang 插件:          1 passed   (准确报告 2 个未使用变量)
Total:              64 组断言, 78 个检查点全部通过
```

---

## 如何使用本教程

1. **顺序学习**：按章节顺序走，每章结束都有小练习。
2. **边读边写**：所有代码片段都建议亲手敲一遍，跑通示例。
3. **跑测试**：`make test` 会跑当前章节对应的测试。
4. **参考文档**：[`docs/reference/`](docs/reference/) 里有两份 ref 的整理稿，遇到不清楚的概念先查那里。

---

## 与参考材料的关系

| 来源 | 在教程中的位置 |
| ---- | -------------- |
| 借助 Lex 和 Yacc 进行词法语法分析（PDF ref 1） | 第 2、6 章实践基础；C1 文法直接取自本文 |
| 《自己动手写编译器》第 11 章（HTML ref 2） | 第 5 章 LR(0) 理论的原始资料 |
