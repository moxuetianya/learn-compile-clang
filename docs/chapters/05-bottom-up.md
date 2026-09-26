# 第 05 章：自底向上 —— LR(0)、SLR、LR(1)、LALR

> 本章是教程理论部分的难点，但也是最有价值的一章。理解 LR 分析，你就理解了 **GCC、Clang、Rust、Go** 等主流编译器语法层的共同基础。
>
> 我们会用一个简化的语法一步步推演，确保你能在纸上算 LR(0) 项目集族。

## 5.1 自底向上到底在做什么

回忆上一章：自顶向下 = 从起始符号 **推导** 出句子。
自底向上 = 反过来，从句子（token 流）出发，**归约** 到起始符号。

归约 = 把产生式右边的符号串替换回左边的非终结符。

例：

```
语法:  E → E + T | T
       T → id

句子:  id + id

归约过程:
id + id  ⇒ T + id   (T → id)
T + id   ⇒ T + T    (T → id)
T + T    ⇒ E + T    (E → T)
E + T    ⇒ E        (E → E + T)        ← 起始符号，归约成功
```

## 5.2 LR 是什么 / 为什么

**LR** = Left-to-right scan, Rightmost derivation (in reverse)
- 从左到右扫描输入
- 还原的是 **最右推导**（在逆方向）

LR 分析的状态 = 栈 + 当前输入 + 状态。核心思想：

- **栈**始终保存一个 **合法前缀**（viable prefix）：语法能接受的前缀
- 每一步：
  - **shift**：把一个 token 压栈
  - **reduce**：栈顶若干符号正好是某个产生式的右边 → 弹出，把左边的非终结符压栈

难的是：**何时 shift、何时 reduce？**

LR 的答案：**用有限状态自动机 + 栈**。状态自动机告诉你下一步该做什么动作。

## 5.3 形态（item / configuration）

LR 分析的核心是 **形态**（item）。

一个产生式 `A → XYZ` 有 4 个可能的形态（黑点表示"分析进度"）：

```
A → ·XYZ       开始
A → X·YZ       已读到 X
A → XY·Z       已读到 X Y
A → XYZ·       全部读完，可归约
```

**可归约形态**（reducible item）就是黑点在最后的形态：表示"右边全部匹配了，可以 reduce"。

形态的作用：**记住"我们在产生式的什么位置"**。这正是 LR 比 DFA 强的地方 —— DFA 只能记住状态，LR 还能记住走到这个状态的"路径"。

## 5.4 形态的转移（successor）

形态 `A → X·YZ` 遇到终结符 `Y` 时，转移到 `A → XY·Z`。

```
A → X·YZ  --Y-->  A → XY·Z
```

## 5.5 形态的延伸（extended item）

如果 `A → ·XYZ` 中黑点后是非终结符 `X`，且 `X → u` 是产生式，那么 `X → ·u` 也是当前形态的"延伸"。

直观理解：你看到 `A` 要展开成 `XYZ`，所以也要准备好 `X` 自己展开的可能性。

## 5.6 项目集（item set）和闭包

**项目集** = 一组形态的集合。
**闭包**（closure）= 项目集中所有形态的所有延伸形态都加进去。

闭包算法：

```
CLOSURE(I):
    repeat
        for each item [A → α·Bβ] in I
            for each production B → γ
                if [B → ·γ] not in I
                    add [B → ·γ] to I
    until I doesn't change
```

## 5.7 状态和后继状态

**状态** = 经过闭包的项目集。每个状态是一个"分析位置"。

**后继状态**（successor）= 当前状态 I 遇到符号 X 时移动黑点后的闭包：

```
NEXT(I, X) = CLOSURE( { NEXT(c, X) | c ∈ I } )
```

**项目集族** = 从起始状态开始，反复求后继，直到不再有新状态。所有这些状态构成有穷自动机。

## 5.8 一个具体例子（来自参考文档）

文法：

```
S → A
A → aBd
B → bc
```

### 步骤 1：起始状态 I0

```
I0 = CLOSURE({ [S → ·A] })
   = { [S → ·A],
       [A → ·aBd] }     ← 因为 A → aBd 是产生式，延伸形态加入
```

### 步骤 2：所有可能的转移

对每个符号 X ∈ {S, A, B, a, b, c, d}，求 NEXT(I0, X)：

```
NEXT(I0, a) = { [A → a·Bd] }       ← 闭包后
NEXT(I0, A) = { [S → A·] }         ← 可归约！
其他符号 NEXT = 空
```

### 步骤 3：扩展

```
I1 = NEXT(I0, A) = CLOSURE({[S → A·]}) = { [S → A·] }
I2 = NEXT(I0, a) = CLOSURE({[A → a·Bd]})
   = { [A → a·Bd],
       [B → ·bc] }    ← 延伸：因为 B 在黑点后
I3 = NEXT(I2, B) = { [A → aB·d] }
I4 = NEXT(I2, b) = { [B → b·c] }
I5 = NEXT(I3, d) = { [A → aBd·] }   ← 可归约
I6 = NEXT(I4, c) = { [B → bc·] }    ← 可归约
```

整个项目集族：

```
        A        a
   ┌──► I1   ┌─► I2 ──B──► I3 ──d──► I5
I0─┤         │                  │
   │         │                  b
   │         │                  ▼
   │         │                 I4 ──c──► I6
   │         │
   │         └─► ...
   └──► (其他符号没转移)
```

### 步骤 4：LR(0) 分析表

| 状态 | A | B | a | b | c | d | $ |
| ---- | - | - | - | - | - | - | - |
| I0 | goto I1 |  | shift I2 |  |  |  |  |
| I1 |  |  |  |  |  |  | **ACCEPT** |
| I2 |  | goto I3 |  | shift I4 |  |  |  |
| I3 |  |  |  |  |  | shift I5 |  |
| I4 |  |  |  |  | shift I6 |  |  |
| I5 | reduce A→aBd |  |  |  |  |  |  |
| I6 | reduce B→bc |  |  |  |  |  |  |

### 步骤 5：分析句子 `abcd`

| 符号栈 | 状态栈 | 当前 | 剩余 | 动作 |
| ------ | ------ | ---- | ---- | ---- |
| | I0 | a | abcd$ | M[I0,a]=shift I2 |
| a | I0 I2 | b | bcd$ | M[I2,b]=shift I4 |
| ab | I0 I2 I4 | c | cd$ | M[I4,c]=shift I6 |
| abc | I0 I2 I4 I6 | d | d$ | M[I6,d]=reduce B→bc |
| a | I0 I2 | B | d$ | M[I2,B]=goto I3 |
| aB | I0 I2 I3 | d | d$ | M[I3,d]=shift I5 |
| aBd | I0 I2 I3 I5 | $ | $ | M[I5,$]=reduce A→aBd |
| | I0 | A | $ | M[I0,A]=goto I1 |
| A | I0 I1 | $ | $ | M[I1,$]=**ACCEPT** |

## 5.9 LR(0) 的限制

LR(0) 要求：项目集中如果含有可归约形态，**不能含有任何其他形态**。
即：要么只 shift，要么只 reduce，**不能又 shift 又 reduce（shift/reduce 冲突）**。

这限制太强，绝大多数程序语言的文法都不满足。

## 5.10 SLR：稍微放松一点

**SLR(1)** = Simple LR：归约前看 1 个 lookahead token。
归约条件：只有当 `lookahead ∈ FOLLOW(A)` 时才用产生式 `A → α` 归约。

**好处**：能消除大多数 shift/reduce 冲突。
**不足**：还有 SLR 解决不了的（如悬空 else）。

## 5.11 LR(1)：完整版

**LR(1)** 形态 = 普通形态 + 一个 lookahead 集合：

```
[A → α·β,  {a, b}]    ← 表示在 lookahead 是 a 或 b 时可以这样
```

更精细，所以威力最强 —— **能分析所有确定性的上下文无关文法**。
代价：项目集可能比 LR(0) 多 10 倍以上，状态表很大。

## 5.12 LALR(1)：实践中的最优解

**LALR(1)** = Look-Ahead LR：把 LR(1) 中"核心形态相同、只 lookahead 不同"的状态合并。
- 比 canonical LR(1) 状态数少得多（通常只多 1~2 倍 LR(0)）
- 比 SLR 强得多（能解决悬空 else）
- **bison 默认就是 LALR(1)**

> 为什么 LALR(1) 在工程上这么受欢迎？因为经验上 **核心相同的状态合并后冲突几乎不增加**，但状态数减少一个量级。

## 5.13 冲突

- **shift/reduce 冲突**：在某个状态，既有可归约形态，又有可移进形态。
  - 例：悬空 else —— 既可以 reduce `if (cond) stmt`，又可以 shift `else`。
- **reduce/reduce 冲突**：多个可归约形态在同一个状态。
  - 通常是文法问题。

冲突不一定是错误 —— 可以加优先级消歧（bison 用 `%left/%right/%nonassoc`），但本质上冲突的文法说明语言不够干净。

## 5.14 现场常用方法的优劣

| 方法 | 优势 | 劣势 | 适用 |
| ---- | ---- | ---- | ---- |
| 手写递归下降 | 控制力最强、错误信息最好 | 文法限制大 | TS、Clang 大部分、V8 |
| bison/yacc (LALR) | 工业标准、改文法快 | 错误信息弱、调试生成的 C 代码难 | GCC、Go、Rust、SQL 引擎 |
| ANTLR (ALL(*)) | 文法表达力强、生成多语言 | 比 LALR 慢、目标代码质量一般 | DSL、教育项目 |
| GLR | 能处理任意 CFG（含歧义） | 实现复杂 | 自然语言、某些研究编译器 |
| PEG | 无歧义、优先级直观 | 不能处理左递归 | 配置文件、新 DSL |

> **GCC/Clang 为啥用手写？** 错误信息的极致要求 + 增量编译性能。
> **Rust 为啥用 LALR？** 文法规模大，但优先级清晰，工具最快产出可用代码。

## 5.15 在纸上做一遍 LR(0)

> 来自参考文档原始例题，建议你自己算一次。

文法：

```
S → A
A → aBd
B → bc
```

题目：
1. 写出 I0 及其闭包
2. 写出所有后继状态（至少 a、A 两条）
3. 画出完整项目集族
4. 构造 LR(0) 分析表（不完整的也行）
5. 用分析表跟踪 `abcd` 的解析过程

> 答案见 [本教程附录 B](../reference/lr0-walkthrough.md)。

## 5.16 小结

- LR 分析的核心是 **项目集 + 闭包 + 后继**
- LR(0) 太弱，SLR 略好，LR(1) 最强但贵，LALR(1) 是性价比之王
- 我们用 bison 走 LALR(1) 路线

**思考题**：
1. 为什么 LR 分析一定要引入"状态"而不是只看 lookahead？
2. 悬空 else 是 shift/reduce 还是 reduce/reduce？bison 怎么消歧？
3. 用 LR(0) 试分析 `id + id + id`，看会不会出错。

---
**下一章**：[第 06 章 语法分析：bison 实践](06-bison-yacc.md)
