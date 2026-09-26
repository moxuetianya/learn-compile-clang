# 附录 A：C1 文法（完整 EBNF 形式）

> 来源：[PDF ref 第 2 节](../reference/ref-pdf-zh.md#二实验内容定义-c1-文法)
> 本附录整理为 EBNF 形式，便于直接转换成 bison/yacc。

## A.1 词法

```
关键字      = "else" | "if" | "int" | "return" | "void" | "while"
专用符号    = "+" | "-" | "*" | "/" | "<" | "<=" | ">" | ">="
            | "==" | "!=" | "&&" | "||" | "=" | ";" | ","
            | "(" | ")" | "[" | "]" | "{" | "}" | "/*" | "*/"
ID         = letter (letter | digit)*
NUM        = digit+
letter     = "a".."z" | "A".."Z"
digit      = "0".."9"
WS         = (空格 | 制表符 | 换行)+
COMMENT    = "/*" ... "*/"   (不可嵌套, 可跨行)
```

## A.2 语法（EBNF 形式）

```
program             = declaration-list

declaration-list    = { declaration }+

declaration         = var-declaration | fun-declaration

var-declaration     = type-specifier identifier ";"
                    | type-specifier identifier "[" integer "]" ";"

type-specifier      = "int" | "void" | "float"

fun-declaration     = type-specifier identifier "(" params ")" compound-stmt

params              = param-list | "void"
param-list          = param { "," param }
param               = type-specifier identifier
                    | type-specifier identifier "[" "]"

compound-stmt       = "{" { var-declaration } statement-list "}"
                    ; 局部声明和语句列表都可为空

statement-list      = { statement }+

statement           = expression-stmt
                    | compound-stmt
                    | selection-stmt
                    | iteration-stmt
                    | return-stmt

expression-stmt     = expression ";"
                    | ";"

selection-stmt      = "if" "(" expression ")" statement
                    | "if" "(" expression ")" statement "else" statement

iteration-stmt      = "while" "(" expression ")" statement

return-stmt         = "return" ";"
                    | "return" expression ";"

expression          = var "=" expression
                    | simple-expression

var                 = identifier
                    | identifier "[" expression "]"

simple-expression   = additive-expression
                    | additive-expression relop additive-expression

relop               = "<=" | "<" | ">=" | ">" | "==" | "!=" | "&&" | "||"

additive-expression = term { addop term }
addop               = "+" | "-"

term                = factor { mulop factor }
mulop               = "*" | "/"

factor              = "(" expression ")"
                    | var
                    | call
                    | integer

call                = identifier "(" args ")"
args                = expression { "," expression }    ; 可为空
```

## A.3 语义约束清单

| # | 约束 |
| - | ---- |
| 1 | 至少有一个声明 |
| 2 | 所有标识符必须先声明后使用 |
| 3 | 程序最后一个声明必须是 `int main() { ... }` |
| 4 | 每个 `var-declaration` 只声明一个变量 |
| 5 | 数组索引范围 `0..NUM-1`，**不进行越界检查** |
| 6 | 函数返回值类型：void 函数可 `return;` 或不 return；非 void 函数必须有 return expr |
| 7 | 数组参数由引用传递，整型参数由值传递 |
| 8 | 数组参数必须是单个 ID（不能用数组字面量） |
| 9 | 实参个数必须与形参个数一致 |
| 10 | `if`、`while` 的条件必须是 int |
| 11 | 关系运算、加法、乘法、赋值的左右操作数必须是 int |
| 12 | `return expr` 中 expr 必须是 int（除非函数返回 void） |

## A.4 bison 输入要点

直接用上面 BNF 形式翻译为 bison 时注意：

1. **保留左递归**：`additive-expression → additive-expression addop term`
   bison 自动处理左递归，不需要手动消除。
2. **悬空 else**：用 `%nonassoc LOWER_THAN_ELSE` + `%nonassoc ELSE` + `%prec LOWER_THAN_ELSE` 解决。
3. **优先级排序**（自上而下递增）：
   ```
   %left  OR             ; ||
   %left  AND            ; &&
   %left  EQ NEQ         ; == !=
   %left  LT LE GT GE    ; < <= > >=
   %left  PLUS MINUS     ; + -
   %left  MUL DIV        ; * /
   %right ASSIGN         ; =
   %nonassoc LOWER_THAN_ELSE
   %nonassoc ELSE
   ```
4. **空产生式**：用 `/* empty */` 或直接空。

## A.5 经典测试用例

### 测试 1：基本运算

```c
int main() {
    int a; int b; int c;
    a = 10;
    b = 20;
    c = a + b * 2;
    return c;
}
```

期望：`c = 50`，返回 50。

### 测试 2：fib

```c
int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int main() {
    return fib(10);
}
```

期望：返回 55。

### 测试 3：数组与 while

```c
int main() {
    int a[10];
    int i; int s;
    i = 0;
    s = 0;
    while (i < 10) {
        a[i] = i * 2;
        s = s + a[i];
        i = i + 1;
    }
    return s;
}
```

期望：`s = 0+2+4+...+18 = 90`。

### 测试 4：函数与数组参数

```c
int sum(int a[], int n) {
    int s; int i;
    s = 0;
    i = 0;
    while (i < n) {
        s = s + a[i];
        i = i + 1;
    }
    return s;
}
int main() {
    int a[5];
    a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4; a[4] = 5;
    return sum(a, 5);
}
```

期望：返回 15。

### 测试 5：if-else 与控制流

```c
int abs(int x) {
    if (x < 0) return -x;
    return x;
}
int main() {
    return abs(-42);
}
```

期望：返回 42。
