/*
 * 独立 driver: 仅验证词法。
 * 编译时与 bison 一起: gcc build/lex.yy.c build/c1.tab.c src/lexer/main.c -lfl -o build/lexer_test
 */

#include <stdio.h>
#include "c1.tab.h"

int yylex(void);

static const char *tok_name(int t) {
    switch (t) {
    case TK_IF: return "TK_IF";
    case TK_ELSE: return "TK_ELSE";
    case TK_INT: return "TK_INT";
    case TK_RETURN: return "TK_RETURN";
    case TK_VOID: return "TK_VOID";
    case TK_WHILE: return "TK_WHILE";
    case TK_PLUS: return "TK_PLUS";
    case TK_MINUS: return "TK_MINUS";
    case TK_MUL: return "TK_MUL";
    case TK_DIV: return "TK_DIV";
    case TK_LE: return "TK_LE";
    case TK_LT: return "TK_LT";
    case TK_GE: return "TK_GE";
    case TK_GT: return "TK_GT";
    case TK_EQ: return "TK_EQ";
    case TK_NEQ: return "TK_NEQ";
    case TK_AND: return "TK_AND";
    case TK_OR: return "TK_OR";
    case TK_ASSIGN: return "TK_ASSIGN";
    case TK_SEMI: return "TK_SEMI";
    case TK_COMMA: return "TK_COMMA";
    case TK_LPAREN: return "TK_LPAREN";
    case TK_RPAREN: return "TK_RPAREN";
    case TK_LBRACK: return "TK_LBRACK";
    case TK_RBRACK: return "TK_RBRACK";
    case TK_LBRACE: return "TK_LBRACE";
    case TK_RBRACE: return "TK_RBRACE";
    case TK_ID: return "TK_ID";
    case TK_NUM: return "TK_NUM";
    case TK_ERROR: return "TK_ERROR";
    case 0: return "EOF";
    default: return "?";
    }
}

int main(void) {
    int t;
    while ((t = yylex()) != 0) {
        if (t == TK_ID)
            printf("%-10s %s\n", tok_name(t), yylval.str);
        else if (t == TK_NUM)
            printf("%-10s %d\n", tok_name(t), yylval.num);
        else
            printf("%-10s\n", tok_name(t));
    }
    return 0;
}
