# 第 14 章：期末项目

> 综合应用前 13 章。选题任选其一，或自拟。

## 14.1 选题列表

### A. 扩展 C1 子集（推荐）

在已实现的 c1c 上加新特性。建议路线：

**A1**：加 `for` 循环
- 文法改 `iteration_stmt` 增加 `for` 分支
- codegen 类似 while，只是初始化在循环前

**A2**：加 `char` 类型和字符串字面值
- `char` = LLVM `i8`
- 字符串 = LLVM `[N x i8]`，全局存储

**A3**：加 `struct`
- 类型系统扩展
- `getelementptr` 处理多字段

**A4**：加 `do-while`、`break`、`continue`
- 控制流加深

**A5**：加 `enum`
- 类型检查 + codegen

### B. Clang 工具

**B1**：自动加注释
- 找到所有函数声明，根据参数名生成 doxygen 注释

**B2**：找潜在 bug
- 比如：所有 `malloc` 后有没有 `free`
- 所有 `fopen` 后有没有 `fclose`

**B3**：统计代码
- 函数平均长度、最大嵌套深度

### C. 解释器路线

**C1**：给 C1 加字节码 + 栈式 VM
- 不用 LLVM，自己写解释器
- 加 GC（标记-清扫）

**C2**：tree-walking 解释器
- 直接遍历 AST 求值
- 最快看到效果

### D. 自选

任何与编译相关的小项目，和老师讨论后做。

## 14.2 评分标准

| 维度 | 占比 | 说明 |
| ---- | ---- | ---- |
| 正确性 | 40% | 测试通过率 |
| 设计 | 20% | 模块化、可读性 |
| 文档 | 15% | README + 关键设计说明 |
| 测试 | 15% | 至少 5 个测试用例 |
| 演示 | 10% | 5 分钟 demo |

## 14.3 提交物

```
project-<name>/
├── README.md             # 项目说明、设计、运行方法
├── src/                  # 源码
├── tests/                # 测试用例
├── docs/                 # 关键决策和遇到的坑
└── examples/             # 示例程序
```

## 14.4 选题建议

| 想做什么 | 难度 | 选 |
| -------- | ---- | -- |
| 学以致用，体验完整编译器 | 中 | A1-A3 |
| 求职方向是 Clang/LLVM | 高 | B 路线 |
| 想做语言/解释器 | 中 | C 路线 |
| 时间紧 | 低 | C2（tree-walking 解释器） |

## 14.5 时间线

| 周 | 任务 |
| -- | ---- |
| 14 | 选题、提交计划 |
| 15 | 实现 + 单元测试 |
| 16 | 完善 + 写文档 |
| 17 | 验收演示 |

## 14.6 一些参考资源

- [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/) —— 手写 LLVM 编译器的官方教程
- [Crafting Interpreters](https://craftinginterpreters.com/) —— Robert Nystrom 的解释器书（强烈推荐）
- [Writing a C Compiler](https://norasandler.com/2017/11/29/Write-a-Compiler.html) —— Nora Sandler 的博客
- [Clang AST 文档](https://clang.llvm.org/docs/IntroductionToTheClangAST.html)

---

# 教程结束

到这里你已经掌握了编译原理的核心概念和实践。剩下的是**自己写一个**。

祝你顺利。

---
**附录**：
- [C1 文法](../reference/c1-grammar.md)
- [LR(0) 详细推演](../reference/lr0-walkthrough.md)
- [第一份 ref 整理稿](../reference/ref-pdf-zh.md)
- [第二份 ref 整理稿](../reference/ref-html-zh.md)
