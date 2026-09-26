# 参考资料 1：借助 Lex 和 Yacc 进行词法语法分析（中文整理稿）

> **原始来源**：https://image.sciencenet.cn/olddata/kexue.com.cn/upload/blog/file/2009/3/2009320124111109950.pdf
> **整理目的**：将 PDF 内容整理为便于检索、复用、对照的 markdown 格式；保留原文主要内容并按章节组织。
>
> **原文位置**：[`ref-pdf-raw.txt`](ref-pdf-raw.txt)（pdftotext 提取的原始文本，1357 行）
>
> 本文主体为原文中译整理；教程中 [第 2 章](../chapters/02-lex-flex.md) 与 [第 6 章](../chapters/06-bison-yacc.md) 大量引用了本文的 C1 文法与示例。

---

## 一、实验目的

1. 通过对实验型程序设计语言 **C1** 的定义，掌握程序设计语言的基本语法和语义；
2. 使用 **Lex** 及 **Yacc** 实现词法分析和语法分析。

## 二、实验内容：定义 C1 文法

### C1 语言惯用的词法

**关键字**（保留字，必须小写）：

```
else  if  int  return  void  while
```

**专用符号**：

```
+  -  *  /  <  <=  >  >=  ==  !=  =  ;  ,  (  )  [  ]  {  }  /*  */
```

**其他标记**（正则表达式定义）：

```
ID   = letter letter*
NUM  = digit  digit*
letter = a|..|z|A|..|Z
digit  = 0|..|9
```

大小写敏感。

**空白**：空格、换行符、制表符，通常被忽略（除非需要分开 ID、NUM、关键字）。

**注释**：`/* ... */`，可跨行，**不能嵌套**。注释可出现在任何空白出现的位置（即不能在 token 中间）。

### C1 文法（完整 BNF）

```
1.  program             → declaration-list
2.  declaration-list    → declaration-list declaration
                         | declaration
3.  declaration         → var-declaration | fun-declaration
4.  var-declaration     → type-specifier ID ';'
                         | type-specifier ID '[' NUM ']' ';'
5.  type-specifier      → int | void | float
6.  fun-declaration     → type-specifier ID '(' params ')' compound-stmt
7.  params              → param-list | void
8.  param-list          → param-list ',' param
                         | param
9.  param               → type-specifier ID
                         | type-specifier ID '[' ']'
10. compound-stmt       → '{' local-declarations statement-list '}'
11. local-declarations  → local-declarations var-declaration | empty
12. statement-list      → statement-list statement | empty
13. statement           → expression-stmt
                         | compound-stmt
                         | selection-stmt
                         | iteration-stmt
                         | return-stmt
14. expression-stmt     → expression ';'
                         | ';'
15. selection-stmt      → 'if' '(' expression ')' statement
                         | 'if' '(' expression ')' statement 'else' statement
16. iteration-stmt      → 'while' '(' expression ')' statement
17. return-stmt         → 'return' ';'
                         | 'return' expression ';'
18. expression          → var '=' expression
                         | simple-expression
19. var                 → ID
                         | ID '[' expression ']'
20. simple-expression   → additive-expression relop additive-expression
                         | additive-expression
21. relop               → '<=' | '<' | '>' | '>=' | '==' | '!='
                         | '&&' | '||'
22. additive-expression → additive-expression addop term
                         | term
23. addop               → '+' | '-'
24. term                → term mulop factor
                         | factor
25. mulop               → '*' | '/'
26. factor              → '(' expression ')'
                         | var
                         | call
                         | NUM
27. call                → ID '(' args ')'
28. args                → arg-list | empty
29. arg-list            → arg-list ',' expression
                         | expression
```

### 语义解释（逐条）

1. **program**：由声明列表组成。
2. **declaration-list**：声明顺序任意，至少有一个声明。
3. **declaration**：声明可以是变量或函数声明。
   - **重要约束**：所有变量和函数在使用前必须声明（避免向后 backpatching 引用）。
   - 程序中最后的声明必须是函数声明，名字为 `main`。
   - C1 没有原型，声明和定义之间没有区别。
4. **var-declaration**：变量声明或简单整型/数组变量。
   - 数组索引范围 `0..NUM-1`。
   - 每个声明只能声明一个变量。
5. **type-specifier**：类型指示符（`int`、`void`、`float`）。
   - C1 仅有的基本类型是整型和空类型。
   - 在变量声明中只能用 `int`；`void` 用于函数声明。
6. **fun-declaration**：函数声明由返回类型、标识符、参数列表和复合语句组成。
   - 如果返回类型是 `void`，函数不返回任何值（即过程）。
   - 参数可以是 `void`（无参数）或参数列表。
   - **数组参数**后面跟 `[]`，其大小是可变的，由引用传递（指针）。
   - **简单整型参数**由值传递。
   - 函数可以是递归的。
7-9. params/param-list/param：见上。
10. **compound-stmt**：复合语句由 `{`、局部声明、语句列表、`}` 组成。
   - 局部声明的作用域等于复合语句的语句列表。
11-12. local-declarations/statement-list：可以为空。
13-14. statement/expression-stmt：表达式语句有一个可选的且后面跟着分号的表达式。
15. **selection-stmt**：`if` 语句。
   - 表达式求值：非 0 引起第一条语句执行，0 值引起第二条执行。
   - **悬空 else**：使用标准方法解决 —— else 部分通常作为当前 if 的一个子结构立即分析（"最近嵌套"非二义性规则）。
16. **iteration-stmt**：`while` 语句。重复执行表达式，0 时结束。
17. **return-stmt**：返回语句。
   - 函数没有说明为 `void` 就必须返回一个值。
   - `return` 引起控制返回调用者（如果在 `main` 中，程序结束）。
18-19. **expression / var**：赋值有通常的存储语义。
   - var 是简单的整型变量或下标数组变量。
   - 负的下标将引起程序停止（与 C 不同）。但**不进行下标越界检查**。
   - 在 C1 中唯一的左值是 `var` 语法给定的，故指针运算是禁止的。
20-21. **simple-expression**：无结合的关系操作符（即无括号的表达式仅有一个关系操作符）。
22-23. **additive-expression**：见上。
24-25. **term / mulop**：`/` 符号表示整数除，余数截去。
26. **factor**：括号表达式、变量、函数调用或 NUM。
27-29. **call / args**：函数调用。
   - 函数在调用之前必须声明，参数数目必须等于调用中参数数目。
   - 函数声明中的数组参数必须和一个表达式匹配，表达式由一个标识符组成表示一个数组变量。

## 三、实验要求

1. 编写 LEX 和 YACC 源文件，实现 C1 语言的词法分析和语法分析功能，最后上机调试通过。
2. 通过现场测试验收：编写测试程序，以给定的测试文件作为输入，输出运行结果到输出文件中。
3. 实验报告按模板填写：
   - a) **功能描述**：程序具有什么功能？
   - b) **程序结构描述**：函数调用格式、参数含义、返回值描述、函数功能；可附调用关系图、流程图。
   - c) **实验总结**：耗时、纸上设计时间、上机调试时间、思考时间、难题与克服方法、对程序的评价、收获。

## 四、评判标准

1. 输出正确的实验结果；
2. 代码清晰，格式良好；
3. 提交的报告内容完整，层次清楚，结论正确。

## 五、程序工作说明（以 C1 语言为例）

1. 用 BNF 或 EBNF 等形式给出 C1 语法的描述；
2. 编写 LEX 源文件，如 `C1.l`；
3. 用文本编辑器编辑相应的 flex 和 bison 文件 `C1.l` 和 `C1.y`；
4. 运行 flex 生成 `lex.yy.c` 文件；
5. 运行 bison 生成 `myyacc.tab.c` 和 `myyacc.tab.h` 文件；
6. 编写其他相关文件 `other.c`、`others.h`；
7. 在 Windows 的 VC++ 开发环境下，新建工程，添加以上文件，编译连接，生成 `C1.exe`；
8. 在命令行模式下运行可执行文件，输入源程序（命令行或文本方式），检查其正确性。

## 六、相关知识（原文中两篇参考文章）

原文在第六部分附了两篇参考文章，本文整理了核心要点（原文较长的部分用摘要代替）。

### 第一篇：yacc 与 Lex 快速入门（要点摘要）

- **目标**：理解 lex/flex + yacc/bison 的整体流程
- **重点**：
  - `.l` 文件的结构：`%{ %}` + `%%` 规则 + `%%` 用户代码
  - `.y` 文件的结构同上
  - 编译流程：`flex` → `lex.yy.c`；`bison -d` → `*.tab.c` + `*.tab.h`
  - 链接：`gcc ... -lfl`（或 `-ll`）
  - `yylval` 用于在 lex 和 yacc 之间传递值
  - `YYSTYPE` 宏用于定义值类型

### 第二篇：使用 yacc 和 lex 编写文本分析器（详细原文摘录）

> 原文较长（约 1200 行），此处整理关键示例和结论。

#### 词法分析入门

什么是词法分析：

> 词法分析首先进行识别输入数据的处理，并且可以使用 lex 工具来完成该处理过程。

#### 一个简单 lex 定义

```lex
%{
#include <stdio.h>
%}

%%
begin printf("Started\n");
hello printf("Hello yourself!\n");
thanks printf("Your welcome\n");
end   printf("Stopped\n");
%%
```

生成与运行：

```bash
$ flex exampleA.l
$ gcc -o exampleA lex.yy.c -lfl
$ exampleA
begin
Started
```

#### 真正的标记化

```lex
%{
#include <stdio.h>
#include <y.tab.h>
%}

%%
begin return BEGIN;
hello return HELLO;
thanks return THANKS;
end   return END;
%%
```

注意：现在识别 token 时是 `return` 标记名，不再是 `printf`。标记名在 `y.tab.h` 里定义（由 yacc 生成）。

#### 提取变量数据

```lex
[0-9]+   { yylval=atoi(yytext); return NUMBER; }
```

`yytext` 是 lex 读取的原始字符串，`yylval` 是 lex 和 yacc 之间交换实际值的桥梁。

#### yacc 语法规则基础

```yacc
addexpr: NUMBER PLUSTOKEN NUMBER
       {
           printf("%f\n",($1+$3));
       }
       ;
```

`$1` 表示产生式右边第一项，`$3` 第三项；`$$` 表示产生式左边的值。

#### 优先级和结合性

```yacc
%token EQUALS POWER
%left PLUS MINUS
%left MULTIPLY DIVIDE
%right NOT
```

- 三种标记类型：`%token`（无优先级）、`%left`（左结合）、`%right`（右结合）。
- **优先级自上而下递增**。
- 同一行的标记优先级相同。

#### 完整的 Makefile 示例

```make
YFLAGS = -d
PROGRAM = calc
OBJS = calcparse.tab.o lex.yy.o fmath.o const.o
SRCS = calcparse.tab.c lex.yy.c fmath.c const.c
CC = gcc

all: $(PROGRAM)

.c.o: $(SRCS)
        $(CC) -c $*.c -o $@ -O

calcparse.tab.c: calcparse.y
        bison $(YFLAGS) calcparse.y

lex.yy.c: lex.l
        flex lex.l

calc: $(OBJS)
        $(CC) $(OBJS) -o $@ -lfl -lm

clean: ; rm -f $(OBJS) core *~ \#* *.o $(PROGRAM) \
        y.* lex.yy.* calcparse.tab.*
```

#### YYSTYPE

```c
#define YYSTYPE double
```

在 lex 和 yacc 之间共享类型定义。

#### 一个复杂的计算器示例（节选 .l 和 .y）

`.l` 节选：

```lex
%{
#define YYSTYPE double
#include "calcparse.tab.h"
#include <math.h>
extern double yylval;
%}

D      [0-9.]

%%
[ \t] { ; }
log     return LOG;
sin     return SIN;
cos     return COS;
{D}+    { sscanf( yytext, "%lf", &yylval ); return NUMBER ; }
"+"     return PLUS;
"-"     return MINUS;
"*"     return MUL;
"/"     return DIV;
"("     return OPENBRACKET;
")"     return CLOSEBRACKET;
"\n"    return EOLN;
```

`.y` 节选：

```yacc
%token NUMBER
%left PLUS MINUS
%left MUL DIV

%%
list:    /* empty */
       | list EOLN
       | list expr EOLN { printf("%g\n", (double)$2); ans=$2; }
       ;
expr:    conditional_expr
       ;
add_expr: mul_expr
       | add_expr PLUS mul_expr { $$ = $1 + $3; }
       | add_expr MINUS mul_expr { $$ = $1 - $3; }
       ;
mul_expr: primary
       | mul_expr MUL primary { $$ = $1 * $3; }
       | mul_expr DIV primary { $$ = $1 / $3; }
       ;
primary: NUMBER { $$ = $1; }
       | OPENBRACKET expr CLOSEBRACKET { $$ = $2; }
       ;
%%
```

#### 进行适当的文本分析（set/get 示例）

```lex
%{
#include <stdio.h>
#include "text.tab.h"
%}

%%
set      return SET;
state    return STATE;
mode     return MODE;
get      return GET;
[a-zA-Z]+ { yylval=strdup(yytext); return STRING; }
%%
```

```yacc
%{
#include <stdlib.h>
#define YYSTYPE char *
char mode[20];
char state[20];
%}

%token SET STATE MODE GET STRING EOLN

%%
list:    /* empty */
       | list getorset
       ;
getorset: getvalue | setvalue;
setvalue: SET STATE STRING { strcpy(state,$3); }
        | SET MODE  STRING { strcpy(mode,$3);  };
getvalue: GET STATE { printf("State: %s\n",state); }
        | GET MODE  { printf("Mode: %s\n",mode);  };
%%
```

#### 语言编译器

> 语言编译器（例如 C 编译器）可以接受您的输入，并将输入转换为指定的目标平台的原始汇编语言。
> 编译器仅为可选的平台生成汇编代码。
> 要将两个数值相加到一起，您可以对前面使用的规则集进行更改，以便生成 Intel x86 汇编代码：

```yacc
add_expr: mul_expr
        | add_expr PLUS mul_expr
          { printf("MOV ax,%d\nADD ax,%d\n",$1,$3); }
```

#### 总结（原文章节）

> 通过 lex 和 yacc 的组合，您可以生成构建分析器的代码。lex 工具提供了识别文本片段和元素的方法，并可返回相应的标记。yacc 工具可以使用一系列描述输入格式的规则来处理这些标记组成的结构，并且定义了处理识别的序列的方法。
>
> 在本教程中，您看到了关于如何使用这些应用程序为计算器应用程序构建简单的分析器的示例。

## 七、参考书目（原文中列出）

1. Kenneth C. Louden 著，冯博琴译《编译原理及实践》，机械工业出版社，2000 年
2. Andrew W. Appel 著，赵克佳等译《现代编译原理 C 语言描述》，人民邮电出版社，2006 年
3. 陈火旺，刘春林等《程序设计语言编译原理》第三版，国防科大出版社，2001 年
4. John R. Levine, Tony Mason, Doug Brown 著，杨作梅等译《Lex 与 Yacc（第二版）》，机械工业出版社，2003 年
5. 王雷等著《编译原理课程设计》，机械工业出版社，2005
6. Lex 与 yacc 快速入门.pdf
7. Lex 与 yacc.pdf
8. flex 和 bison 工具附带的文档说明

---

## 与本教程的对应关系

| 原 PDF 内容 | 本教程对应章节 |
| ----------- | -------------- |
| C1 词法规则 | [第 2 章 词法 flex 实践](../chapters/02-lex-flex.md) |
| C1 语法 | [附录 A C1 文法](c1-grammar.md) |
| lex/yacc 工作流 | [第 0 章 概览](../chapters/00-overview.md)、[第 6 章 bison 实践](../chapters/06-bison-yacc.md) |
| Makefile 示例 | [第 6 章 bison 实践](../chapters/06-bison-yacc.md) |
| 计算器实例 | [第 4 章 递归下降](../chapters/04-top-down.md) |

完整原文（pdftotext 提取）：见 [`ref-pdf-raw.txt`](ref-pdf-raw.txt)。
