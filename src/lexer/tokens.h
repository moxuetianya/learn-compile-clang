/*
 * 共享 token 枚举 —— 手写 lexer 和 bison 都用
 *
 * - TK_* : 通用 token 值, 任何 lexer 都返回这些常量
 * - bison 的 c1.tab.h 也生成同名 enum (api.token.prefix="TK_"), 由于数值
 *   会冲突, 手写 lexer 路径下我们不依赖 bison, 用 TK_* (值 0..29)
 * - ast.c 通过同时列出 TK_PLUS 和 HW_TK_PLUS 两种值来兼容
 */

#ifndef C1C_TOKENS_H
#define C1C_TOKENS_H

typedef enum {
    TK_IF, TK_ELSE, TK_INT, TK_RETURN, TK_VOID, TK_WHILE,
    TK_ID, TK_NUM,
    TK_PLUS, TK_MINUS, TK_MUL, TK_DIV,
    TK_LE, TK_LT, TK_GE, TK_GT, TK_EQ, TK_NEQ, TK_AND, TK_OR, TK_ASSIGN,
    TK_SEMI, TK_COMMA,
    TK_LPAREN, TK_RPAREN, TK_LBRACK, TK_RBRACK, TK_LBRACE, TK_RBRACE,
    TK_EOF, TK_ERROR
} Tok;

/* 手写 lexer 路径使用的一组偏移 token 值, 避免与 bison 数值冲突 */
typedef enum {
    HW_TK_IF = 1000, HW_TK_ELSE, HW_TK_INT, HW_TK_RETURN, HW_TK_VOID, HW_TK_WHILE,
    HW_TK_ID, HW_TK_NUM,
    HW_TK_PLUS, HW_TK_MINUS, HW_TK_MUL, HW_TK_DIV,
    HW_TK_LE, HW_TK_LT, HW_TK_GE, HW_TK_GT,
    HW_TK_EQ, HW_TK_NEQ, HW_TK_AND, HW_TK_OR, HW_TK_ASSIGN,
    HW_TK_SEMI, HW_TK_COMMA,
    HW_TK_LPAREN, HW_TK_RPAREN, HW_TK_LBRACK, HW_TK_RBRACK, HW_TK_LBRACE, HW_TK_RBRACE,
    HW_TK_EOF, HW_TK_ERROR
} HwTok;

#endif
