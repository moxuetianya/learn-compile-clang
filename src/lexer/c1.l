%{
/*
 * C1 词法分析器 — flex 源文件
 * 配套 bison 源文件 src/parser/c1.y
 *
 * 编译:
 *   bison -d src/parser/c1.y -o build/c1.tab.c
 *   flex  -o build/lex.yy.c src/lexer/c1.l
 *   gcc   build/lex.yy.c build/c1.tab.c src/lexer/main.c -o build/lexer_test -lfl
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c1.tab.h"

/* 行号、列号 */
int yycolumn = 1;

#define YY_USER_ACTION \
    do { \
        yylloc.first_line   = yylloc.last_line = yylineno; \
        yylloc.first_column = yycolumn; \
        yylloc.last_column  = yycolumn + yyleng - 1; \
        yycolumn += yyleng; \
    } while (0);
%}

DIGIT    [0-9]
LETTER   [a-zA-Z]
ID       {LETTER}({LETTER}|{DIGIT})*
NUM      {DIGIT}+
WS       [ \t\r]+
COMMENT  "/*"([^*]|\*+[^*/])*"*"+"/"

%%

"if"        return TK_IF;
"else"      return TK_ELSE;
"int"       return TK_INT;
"return"    return TK_RETURN;
"void"      return TK_VOID;
"while"     return TK_WHILE;

"+"         return TK_PLUS;
"-"         return TK_MINUS;
"*"         return TK_MUL;
"/"         return TK_DIV;

"<="        return TK_LE;
"<"         return TK_LT;
">="        return TK_GE;
">"         return TK_GT;
"=="        return TK_EQ;
"!="        return TK_NEQ;
"&&"        return TK_AND;
"||"        return TK_OR;
"="         return TK_ASSIGN;

";"         return TK_SEMI;
","         return TK_COMMA;
"("         return TK_LPAREN;
")"         return TK_RPAREN;
"["         return TK_LBRACK;
"]"         return TK_RBRACK;
"{"         return TK_LBRACE;
"}"         return TK_RBRACE;

{ID}        { yylval.str = strdup(yytext); return TK_ID; }
{NUM}       { yylval.num = atoi(yytext); return TK_NUM; }

{WS}        { /* ignore */ }
{COMMENT}   ;

"\n"        { yylineno++; yycolumn = 1; }

.           { fprintf(stderr, "Lex error at line %d: %s\n", yylineno, yytext); return TK_ERROR; }

%%

int yywrap(void) { return 1; }
