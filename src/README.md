# src/ — c1c 编译器源码

## 目录

```
src/
├── lexer/
│   ├── c1.l              # flex 词法源文件 (bison 路径)
│   ├── main.c            # 独立 lexer 测试 driver
│   ├── tokens.h          # 共享 token enum (TK_* / HW_TK_*)
│   └── handwritten.c     # 纯 C 手写 lexer+parser (主路径)
├── parser/
│   └── c1.y              # bison 语法源文件
├── ast/
│   ├── ast.h             # AST 节点类型 + 构造器声明
│   └── ast.c             # AST 节点构造器 + 输出
├── sema/
│   ├── sema.h            # 符号表 + 类型系统
│   └── sema.c            # 语义分析实现
├── codegen/
│   ├── codegen.c         # 教学版: 直接打印 LLVM IR 文本
│   └── README.md
└── driver/
    ├── main.c            # 通用 main (bison 路径)
    ├── main_hand.c       # 手写版本的 main
    ├── main_emit.c       # IR 输出版本的 main
    └── main_sema.c       # 语义分析版本的 main
```

## 编译目标

| 目标 | 文件 | 依赖 |
| ---- | ---- | ---- |
| `c1c_hand` | handwritten + ast + main_hand | 无 |
| `c1c_sema` | handwritten + ast + sema + main_sema | 无 |
| `c1c_emit` | handwritten + ast + codegen + main_emit | 无 |
| `c1c` | lex.yy.c + c1.tab.c + ast + main | flex, bison |
| `lexer_test` | lex.yy.c + c1.tab.c + main_lexer | flex, bison |
| `parser_test` | lex.yy.c + c1.tab.c + ast | flex, bison |

## 编译命令

```bash
# 默认构建
make c1c_hand c1c_sema

# 如果装了 flex/bison
make c1c

# 跑全部测试
make test
```

## 数据流

```
.c1 文件
  ↓ stdin
lexer (handwritten / flex)
  ↓ token stream
parser (handwritten / bison)
  ↓ AST (program_root)
[可选] 语义分析 (sema)
  ↓ 类型信息 + 错误
[可选] 代码生成 (codegen)
  ↓ LLVM IR 文本
stdout → llc → 汇编 → clang → 可执行
```

## 关键设计

### Token 命名

- `tokens.h` 定义了两个 enum：
  - `Tok` (TK_*): 通用名字, bison 路径用 bison 自动生成的 enum (值 258+)
  - `HwTok` (HW_TK_*): 手写路径专用, 值 1000+, 避免与 bison 冲突
- `ast.c` 的 `op_name()` 用 switch 覆盖两种值, 都能打印出 `+`/`-`/...

### AST 节点

- 节点用一个 `ASTNode` 结构 + tagged union
- `program_root` 是全局变量, parser 填充, driver 取出

### 作用域

- `sema.c` 用链式作用域栈
- 每进一个 compound / function body 就 `sema_enter_scope()`
- 退出时 `sema_leave_scope()` 释放当前层符号

### 错误恢复

- Parser: 在 `error SEMI` 上同步
- Sema: 错误不中断,继续分析,最后报总错误数
