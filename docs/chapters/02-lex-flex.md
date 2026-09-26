# 第 02 章：词法分析 —— flex 实践

> 本章把第 1 章的理论落地。我们用 flex 写 C1 的词法分析器，能识别 C1 全部 token，并配合一段最小测试程序验证。

## 2.1 flex 是什么

- GNU 版的 `lex`，生成 C 代码
- 输入：`.l` 文件（一组"模式 + 动作"）
- 输出：`lex.yy.c`，里面有一个 `yylex()` 函数
- 链接时需要 `lfl` 库（提供 `main`）或自己写 `main`

```
c1.l ──flex──► lex.yy.c ──gcc──► a.out
```

## 2.2 安装

```bash
# Ubuntu/Debian
sudo apt install flex bison

# macOS（自带或 brew install flex bison）
```

## 2.3 flex 文件结构

```
%{              ← C 代码段（直接拷贝到生成的 .c 里）
#include ...
%}

%%              ← 规则段（模式 { 动作 }）
"if"            return IF;
[a-z]+          printf("ID: %s\n", yytext);
%%              ← 用户代码段（main 等）

int main(void) { yylex(); return 0; }
```

注意：
- 模式默认是 **最长匹配** + **先定义者优先**
- 动作是任意 C 代码
- `yytext` 是当前匹配的字符串，`yyleng` 是长度

## 2.4 第一个例子

`src/lexer/hello.l`：

```c
%{
#include <stdio.h>
%}

%%
"hello"     printf("HELLO\n");
"world"     printf("WORLD\n");
[a-zA-Z]+   printf("WORD: %s\n", yytext);
[ \t\n]+    /* 忽略空白 */
.           printf("OTHER: %c\n", yytext[0]);
%%

int main(int argc, char **argv) {
    yylex();
    return 0;
}
```

```bash
$ flex hello.l
$ gcc lex.yy.c -o hello -lfl
$ echo "hello world hello" | ./hello
HELLO
WORD: world
HELLO
```

## 2.5 C1 词法分析器

我们项目里要写的 `src/lexer/c1.l`。下面拆解讲解。

### 2.5.1 头与定义段

```c
%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c1.tab.h"   /* bison 生成的 token 定义（先编译 bison 后才有） */

int yycolumn = 1;     /* 当前列号（用于错误信息） */

#define YY_USER_ACTION \
    yylloc->first_line = yylloc->last_line = yylineno; \
    yylloc->first_column = yycolumn; \
    yylloc->last_column = yycolumn + yyleng - 1; \
    yycolumn += yyleng;
%}

/* 关键字单独列出 */
%token IF ELSE INT RETURN VOID WHILE
%token ID NUM

/* flex 自己的定义段 */
DIGIT    [0-9]
LETTER   [a-zA-Z]
ID       {LETTER}({LETTER}|{DIGIT})*
NUM      {DIGIT}+
WS       [ \t\r]+
COMMENT  "/*"([^*]|\*+[^*/])*"*"+"/"
```

> 我们这里把 `%token` 直接写在 `.l` 里 —— 是为了让 flex 在没有 bison 时也能单独编译测试。实战中 token 由 bison 定义，flex 通过 `%option bison-bridge` 或 `#include "y.tab.h"` 引用。

### 2.5.2 规则段

```c
%%

"if"        return IF;
"else"      return ELSE;
"int"       return INT;
"return"    return RETURN;
"void"      return VOID;
"while"     return WHILE;

"+"         return PLUS;
"-"         return MINUS;
"*"         return MUL;
"/"         return DIV;

"<="        return LE;
"<"         return LT;
">="        return GE;
">"         return GT;
"=="        return EQ;
"!="        return NEQ;
"&&"        return AND;
"||"        return OR;
"="         return ASSIGN;

";"         return SEMI;
","         return COMMA;
"("         return LPAREN;
")"         return RPAREN;
"["         return LBRACK;
"]"         return RBRACK;
"{"         return LBRACE;
"}"         return RBRACE;

{ID}        { yylval.str = strdup(yytext); return ID; }
{NUM}       { yylval.num = atoi(yytext); return NUM; }

{WS}        { /* 忽略空白 */ }
{COMMENT}   { /* 忽略注释 */ }

"\n"        { yycolumn = 1; }   /* flex 自己会更新 yylineno */

.           { fprintf(stderr, "Lex error at line %d: %s\n",
                    yylineno, yytext); return ERROR; }
%%

int yywrap(void) { return 1; }
```

### 2.5.3 几个关键细节

**1) `yylval`**
用来在词法和语法之间传值。flex 默认 `yylval` 是 `int`；实际我们希望：
- ID 传字符串
- NUM 传整数
- 语法树节点传指针

所以需要：

```c
/* 在 .l 和 .y 共享的 .h 里 */
typedef union {
    int     num;
    char*   str;
    /* 后续章节加 AST 节点指针 */
    struct ASTNode* ast;
} YYSTYPE;
#define YYSTYPE YYSTYPE
```

**2) 注释的正则**

```
"/*"([^*]|\*+[^*/])*"*"+"/"
```

解释：
- `"/*"` 起始
- `([^*]|\*+[^*/])*` 任意非 `*` 字符 或 一串 `*` 但后面不跟 `/`
- `"*"+"/"` 结束 —— 至少一个 `*` 加 `/`
- **不匹配嵌套注释**（C 标准行为）

**3) `YY_USER_ACTION`**

每次匹配到一个 token 之前都会执行。能用来维护行列号、debug 日志等。

**4) `yywrap()`**
返回 1 表示"输入结束了"。多文件扫描时返回 0 + 打开下一个文件。

## 2.6 编译与运行

`scripts/build_lexer.sh`：

```bash
#!/usr/bin/env bash
set -e
SRC=src/lexer/c1.l
bison -d src/parser/c1.y -o build/c1.tab.c     # 提前生成 token 名
flex  -o build/lex.yy.c "$SRC"
gcc   build/lex.yy.c build/c1.tab.c main.c -o build/lexer_test -lfl
```

独立的 main（`src/lexer/main.c`）：

```c
#include <stdio.h>
#include "c1.tab.h"

int yylex(void);   /* 由 lex.yy.c 提供 */

int main(void) {
    int t;
    while ((t = yylex()) != 0) {
        const char *name =
            t == IF ? "IF" :
            t == ID ? "ID" :
            t == NUM ? "NUM" :
            "<other>";
        if (t == ID) printf("%-12s %s\n", name, yylval.str);
        else if (t == NUM) printf("%-12s %d\n", name, yylval.num);
        else printf("%-12s\n", name);
    }
    return 0;
}
```

测试输入 `tests/lexer_test.c1`：

```c
int main(void) {
    int x; int y;
    x = 10;
    y = x * 2 + 1;
    if (y >= 20 && x != 0) {
        y = y - 1;
    }
    return y;
}
```

运行：

```bash
$ make lexer
$ build/lexer < tests/lexer_test.c1
INT          (keyword)
ID           main
LPAREN
VOID
RPAREN
LBRACE
INT          (keyword)
ID           x
SEMI
...
```

## 2.7 错误处理技巧

词法错误的常见原因：
- 不认识的字符（中文标点、emoji）
- 未闭合的字符串字面值
- 未闭合的注释

实战策略：
1. **不要静默吞掉** —— 用 `.` 通配规则打印错误信息
2. **记录位置** —— `yylloc` 至少带 `first_line`、`first_column`
3. **不要立即退出** —— 继续往后扫描，多收集错误，最后一起报
4. **错误恢复** —— `yyless()` 把一部分吐回去

## 2.8 现场常用 flex 模式

| 模式 | 用途 |
| ---- | ---- |
| `%option noyywrap` | 多文件输入 |
| `%option bison-bridge bison-locations` | 与 bison 集成位置信息 |
| `%option reentrant` | 生成可重入的 scanner（C++/线程场景） |
| `%array` | `yytext` 改成固定大小数组（默认是指针） |
| `%state COMMENT` | 注释嵌套（手动状态机） |
| `yyless(n)` | 把当前 token 保留前 n 个，其余退回输入流 |
| `yymore()` | 下一个 token 拼接在当前 token 之后 |

## 2.9 小结

- flex 把"正则 → C 代码"的过程自动化
- 我们用 flex 实现了 C1 的全部 token
- 通过 `yylval` 把值传给 bison
- `YY_USER_ACTION` + `yylloc` 维护行列号

**练习**：
1. 在 `c1.l` 里加上对 `// 单行注释` 的支持（C1 没有，但要你改对）。
2. 修改 `yyerror`，让它能输出"第几行第几列附近出错"。
3. 用 `flex --debug` 跑一次，看生成的 DFA 状态机长什么样。

---
**下一章**：[第 03 章 语法分析：理论](03-parse-theory.md)
