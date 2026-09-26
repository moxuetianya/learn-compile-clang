# 第 06 章：语法分析 —— bison 实践

> 上一章用纸和笔算 LR(0)。这一章我们用 **bison**（GNU 版的 yacc）直接生成 LALR(1) 语法分析器，处理整个 C1 文法。

## 6.1 bison 是什么

- GNU 版 yacc
- 输入：`.y` 文件（产生式 + 动作）
- 输出：`y.tab.c`（或 `xx.tab.c`）+ 可选 `y.tab.h`
- 默认生成 **LALR(1)** 分析器

```
c1.y ──bison -d──► c1.tab.c ──gcc──► parser
                    c1.tab.h
```

## 6.2 bison 文件结构

```
%{
   /* C 代码段：include、YYSTYPE 定义 */
%}

/* 声明段：token、类型、优先级 */
%token <str>  ID
%token <num>  NUM
%type  <ast>  expr stmt

/* 优先级（自下而上递增） */
%left  OR
%left  AND
%left  EQ NEQ
%left  LT LE GT GE
%left  PLUS MINUS
%left  MUL DIV
%right ASSIGN

%%

/* 规则段 */
program: decl_list { $$ = $1; }
       ;
decl_list: decl_list decl { ... }
         |                { ... }
         ;

%%

/* 用户代码段：yyerror、main */
```

## 6.3 C1 文法直接落地

把 [C1 文法](../reference/c1-grammar.md) 翻译成 bison。先去掉左递归的版本如下：

```yacc
/* program & declarations */
program          : decl_list                       { $$ = $1; }
                 ;
decl_list        : decl_list decl                  { $$ = append_decl($1, $2); }
                 | /* empty */                     { $$ = NULL; }
                 ;
decl             : var_decl                        { $$ = $1; }
                 | fun_decl                        { $$ = $1; }
                 ;
var_decl         : type_spec ID ';'                { $$ = mk_var_decl($1, $2, NULL, @1); }
                 | type_spec ID '[' NUM ']' ';'    { $$ = mk_var_decl($1, $2, $4, @1); }
                 ;
type_spec        : INT                             { $$ = T_INT;  }
                 | VOID                            { $$ = T_VOID; }
                 ;
fun_decl         : type_spec ID '(' params ')' compound_stmt
                                                      { $$ = mk_fun_decl($1, $2, $4, $6, @1); }
                 ;
params           : param_list                      { $$ = $1; }
                 | VOID                            { $$ = NULL; }
                 ;
param_list       : param_list ',' param            { $$ = append_param($1, $3); }
                 | param                           { $$ = mk_param_list($1); }
                 ;
param            : type_spec ID                    { $$ = mk_param($1, $2, false, @1); }
                 | type_spec ID '[' ']'            { $$ = mk_param($1, $2, true,  @1); }
                 ;

/* statements */
compound_stmt    : '{' local_decls stmt_list '}'   { $$ = mk_block($2, $3, @1); }
                 ;
local_decls      : local_decls var_decl            { ... }
                 | /* empty */
                 ;
stmt_list        : stmt_list stmt                  { ... }
                 | /* empty */
                 ;
stmt             : expr_stmt                       { ... }
                 | compound_stmt                   { ... }
                 | selection_stmt                  { ... }
                 | iteration_stmt                  { ... }
                 | return_stmt                     { ... }
                 ;
expr_stmt        : expr ';'                        { ... }
                 | ';'                             { ... }
                 ;
selection_stmt   : IF '(' expr ')' stmt            { ... }
                 | IF '(' expr ')' stmt ELSE stmt  { ... }
                 ;
iteration_stmt   : WHILE '(' expr ')' stmt         { ... }
                 ;
return_stmt      : RETURN ';'                      { ... }
                 | RETURN expr ';'                 { ... }
                 ;

/* expressions —— 用优先级解决结合性和二义性 */
expr             : var '=' expr                    { $$ = mk_assign($1, $3); }
                 | simple_expr                     { $$ = $1; }
                 ;
var              : ID                              { $$ = mk_var($1, NULL); }
                 | ID '[' expr ']'                 { $$ = mk_var($1, $3); }
                 ;
simple_expr      : add_expr relop add_expr         { $$ = mk_binop($2, $1, $3); }
                 | add_expr                        { $$ = $1; }
                 ;
relop            : LE | LT | GE | GT | EQ | NEQ    { $$ = $1; }
                 ;
add_expr         : add_expr '+' mul_expr           { $$ = mk_binop(T_ADD, $1, $3); }
                 | add_expr '-' mul_expr           { $$ = mk_binop(T_SUB, $1, $3); }
                 | mul_expr                        { $$ = $1; }
                 ;
mul_expr         : mul_expr '*' primary            { $$ = mk_binop(T_MUL, $1, $3); }
                 | mul_expr '/' primary            { $$ = mk_binop(T_DIV, $1, $3); }
                 | primary                         { $$ = $1; }
                 ;
primary          : '(' expr ')'                    { $$ = $2; }
                 | var                             { $$ = $1; }
                 | call                            { $$ = $1; }
                 | NUM                             { $$ = mk_num($1); }
                 ;
call             : ID '(' args ')'                 { $$ = mk_call($1, $3); }
                 ;
args             : arg_list                        { $$ = $1; }
                 | /* empty */                     { $$ = NULL; }
                 ;
arg_list         : arg_list ',' expr               { ... }
                 | expr                            { ... }
                 ;
```

> **关键技巧**：左递归保持原样，bison 不怕；优先级声明放在产生式后面 bison 会自动用。

## 6.4 优先级与结合性

bison 遇到歧义（shift/reduce 或 reduce/reduce 冲突）时：
- 用 `%left`、`%right`、`%nonassoc` 声明优先级
- **后声明的优先级更高**
- `'+'` 和 `'-'` 同优先级（左结合）：`a - b - c = (a-b)-c`
- 隐含：未声明的 token 优先级低于所有声明的

**悬空 else 怎么消？**

```yacc
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

stmt: IF '(' expr ')' stmt                    %prec LOWER_THAN_ELSE
    | IF '(' expr ')' stmt ELSE stmt
    ;
```

`%prec` 让 `IF '(' expr ')' stmt` 的优先级人为压低，遇到 `ELSE` 时优先 shift。

## 6.5 值传递：`yylval` 和 `$$/$1/$2...`

- `yylval`：flex 写到这儿，bison 读
- `$1, $2, ...`：产生式右边的第 1、2 个符号对应的值
- `$$`：产生式左边符号对应的值（**要赋**）
- `-1` 结尾访问：`-1` 表示"$1 之前那个"，用于 mid-rule actions

```yacc
expr: expr '+' expr { $$ = $1 + $3; }
```

**类型**：通过 `%type <T>` 指定每个非终结符的值的类型；终结符用 `%token <T>`。

```c
%union {
    int  num;
    char *str;
    struct AST *ast;
}

%token <str> ID
%token <num> NUM
%type  <ast> expr stmt
```

## 6.6 位置信息：`@$`

每个 yylval 对应一个 yylloc，记录行/列。

```yacc
expr: expr '+' expr { $$ = mk_binop(T_ADD, $1, $3);
                      @$.first_line = @1.first_line;
                      @$.last_line  = @3.last_line; }
```

更简单：用 `%define parse.error verbose` 和 `yyerror(s, loc)` 接 location。

## 6.7 错误恢复

bison 的错误恢复机制 —— `error` token：

```yacc
stmt: ';'
    | expr ';'
    | WHILE '(' expr ')' stmt
    | IF '(' expr ')' stmt
    | IF '(' expr ')' stmt ELSE stmt
    | RETURN ';'  /* 故意吞一个错误 */
    | error ';'                   /* 跳到下一个分号 */
    ;

compound_stmt: '{' local_decls stmt_list '}'
             | '{' error '}'                       /* 复合语句出错，跳到闭合 } */
             ;
```

错误恢复策略：
1. 弹出栈直到出现一个状态含有 `error` 产生式
2. 丢弃 token 直到遇到能继续分析的
3. 假装 `error` 在那里，规约
4. 恢复正常分析

> 这就是为什么你在 GCC 看到一个错误还能继续报后面几十个 —— 都是 `error` token 在干活。

## 6.8 编译与运行

```bash
bison -d -o build/c1.tab.c src/parser/c1.y
flex  -o build/lex.yy.c src/lexer/c1.l
gcc   -I build build/c1.tab.c build/lex.yy.c src/main.c -o build/c1c -lfl
./build/c1c < examples/fib.c1
```

## 6.9 实战：从 C1 源程序到 AST

最小 driver：

```c
#include <stdio.h>
#include "ast.h"

extern int yyparse(void);
extern AST* program_root;

int main(void) {
    if (yyparse() == 0) {
        print_ast(program_root, 0);
        return 0;
    }
    fprintf(stderr, "Parse failed\n");
    return 1;
}
```

bison 里：

```yacc
program: decl_list { program_root = $1; $$ = $1; }
       ;
```

## 6.10 一个常见的 bison 坑

**`%prec`**：让人为优先级覆盖产生式自动计算的优先级。

```yacc
expr: '-' expr %prec UMINUS { $$ = negate($2); }
    ;
```

避免一元负号和二元减号产生冲突。

## 6.11 调试小技巧

```bash
bison -d --report=all --graph src/parser/c1.y
# 生成 c1.output 和 c1.dot
dot -Tpng c1.dot -o c1.png    # 用 graphviz 看状态机
```

`.output` 文件里详细列出了每个状态的形态、冲突、归约依据。**这是调 bison 的核武器。**

## 6.12 小结

- bison 是 LALR(1) 生成器
- 优先级、结合性、%prec 是消歧三大武器
- `%token/%type <T>` 把值绑到语法符号
- `error` token 做错误恢复
- `--report=all` 生成的 `.output` 是调试神器

**练习**：
1. 把 `selection_stmt` 改成不带 `else`，运行 bison 看会不会报 shift/reduce 冲突。
2. 故意引入一个歧义文法，让 bison 报 reduce/reduce 冲突，看 `.output` 里哪个状态出了问题。
3. 在 `expr_stmt` 里加 `error ';'` 规则，测试错误恢复。

---
**下一章**：[第 07 章 抽象语法树 AST](07-ast.md)
