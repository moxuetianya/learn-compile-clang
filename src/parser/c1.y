/*
 * C1 语法分析器 — bison 源文件
 * 配套 flex 源文件 src/lexer/c1.l
 *
 * 编译:
 *   bison -d src/parser/c1.y -o build/c1.tab.c
 *   flex  -o build/lex.yy.c src/lexer/c1.l
 *   gcc   build/lex.yy.c build/c1.tab.c src/ast/ast.c -o build/parser_test
 */

%define parse.error verbose
%locations

%code requires {
#include "ast.h"
}

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

int yylex(void);
void yyerror(const char *s);

ASTNode *program_root = NULL;
int n_errors = 0;
%}

%union {
    int      num;
    char    *str;
    ASTNode *ast;
}

%token <str> TK_ID
%token <num> TK_NUM

%token TK_IF TK_ELSE TK_INT TK_RETURN TK_VOID TK_WHILE
%token TK_PLUS TK_MINUS TK_MUL TK_DIV
%token TK_LE TK_LT TK_GE TK_GT TK_EQ TK_NEQ TK_AND TK_OR TK_ASSIGN
%token TK_SEMI TK_COMMA TK_LPAREN TK_RPAREN TK_LBRACK TK_RBRACK TK_LBRACE TK_RBRACE
%token TK_ERROR

%type <ast> program decl_list decl var_decl fun_decl
%type <num> type_spec
%type <ast> params param_list param
%type <ast> compound_stmt local_decls stmt_list stmt
%type <ast> expr_stmt selection_stmt iteration_stmt return_stmt
%type <ast> expr var simple_expr add_expr mul_expr primary maybecall_args nonempty_x
%type <num> relop

%right TK_ASSIGN
%left  TK_OR
%left  TK_AND
%left  TK_EQ TK_NEQ
%left  TK_LT TK_LE TK_GT TK_GE
%left  TK_PLUS TK_MINUS
%left  TK_MUL TK_DIV
%right UMINUS
%nonassoc LOWER_THAN_ELSE
%nonassoc TK_ELSE

%%

program
    : decl_list                       { program_root = $1; $$ = $1; }
    ;

decl_list
    : decl                            { $$ = mk_decl_list(1, $1); }
    | decl_list decl                  { $$ = append_decl($1, $2); }
    ;

decl
    : var_decl                        { $$ = $1; }
    | fun_decl                        { $$ = $1; }
    ;

var_decl
    : type_spec TK_ID TK_SEMI         { $$ = mk_var_decl($2, $1, 0, @1.first_line); }
    | type_spec TK_ID TK_LBRACK TK_NUM TK_RBRACK TK_SEMI
                                       { $$ = mk_var_decl($2, $1, $4, @1.first_line); }
    ;

type_spec
    : TK_INT                          { $$ = T_INT; }
    | TK_VOID                         { $$ = T_VOID; }
    ;

fun_decl
    : type_spec TK_ID TK_LPAREN params TK_RPAREN compound_stmt
                                       { $$ = mk_fun_decl($2, $1, $4, $6, @1.first_line); }
    ;

params
    : param_list                      { $$ = $1; }
    | TK_VOID                         { $$ = NULL; }
    | /* empty */                     { $$ = NULL; }
    ;

param_list
    : param                           { $$ = mk_param_list($1); }
    | param_list TK_COMMA param       { $$ = append_param($1, $3); }
    ;

param
    : type_spec TK_ID                 { $$ = mk_param($2, $1, 0, @1.first_line); }
    | type_spec TK_ID TK_LBRACK TK_RBRACK
                                       { $$ = mk_param($2, $1, 1, @1.first_line); }
    ;

compound_stmt
    : TK_LBRACE local_decls stmt_list TK_RBRACE
                                       { $$ = mk_compound($2, $3, @1.first_line); }
    ;

local_decls
    : /* empty */                     { $$ = NULL; }
    | local_decls var_decl            { $$ = append_local($1, $2); }
    ;

stmt_list
    : /* empty */                     { $$ = NULL; }
    | stmt_list stmt                  { $$ = append_stmt($1, $2); }
    ;

stmt
    : expr_stmt                       { $$ = $1; }
    | compound_stmt                   { $$ = $1; }
    | selection_stmt                  { $$ = $1; }
    | iteration_stmt                  { $$ = $1; }
    | return_stmt                     { $$ = $1; }
    | error TK_SEMI                   { yyerrok; $$ = NULL; }
    ;

expr_stmt
    : expr TK_SEMI                    { $$ = mk_expr_stmt($1, @1.first_line); }
    | TK_SEMI                         { $$ = NULL; }
    ;

selection_stmt
    : TK_IF TK_LPAREN expr TK_RPAREN stmt                              %prec LOWER_THAN_ELSE
                                       { $$ = mk_if($3, $5, NULL, @1.first_line); }
    | TK_IF TK_LPAREN expr TK_RPAREN stmt TK_ELSE stmt
                                       { $$ = mk_if($3, $5, $7, @1.first_line); }
    ;

iteration_stmt
    : TK_WHILE TK_LPAREN expr TK_RPAREN stmt
                                       { $$ = mk_while($3, $5, @1.first_line); }
    ;

return_stmt
    : TK_RETURN TK_SEMI               { $$ = mk_ret(NULL, @1.first_line); }
    | TK_RETURN expr TK_SEMI          { $$ = mk_ret($2, @1.first_line); }
    ;

expr
    : var TK_ASSIGN expr              { $$ = mk_assign($1, $3, @1.first_line); }
    | simple_expr                     { $$ = $1; }
    ;

maybecall_args
    : nonempty_x                      { $$ = $1; }
    | /* empty */                     { $$ = NULL; }
    ;

nonempty_x
    : expr                            { $$ = mk_arg_list($1); }
    | nonempty_x TK_COMMA expr        { $$ = append_arg($1, $3); }
    ;

var
    : TK_ID                           { $$ = mk_var($1, NULL, @1.first_line); }
    | TK_ID TK_LBRACK expr TK_RBRACK  { $$ = mk_var($1, $3, @1.first_line); }
    | TK_ID TK_LPAREN maybecall_args TK_RPAREN { $$ = mk_call($1, $3, @1.first_line); }
    ;

simple_expr
    : add_expr relop add_expr         { $$ = mk_binop($2, $1, $3, @1.first_line); }
    | add_expr                        { $$ = $1; }
    ;

relop
    : TK_LE                           { $$ = TK_LE; }
    | TK_LT                           { $$ = TK_LT; }
    | TK_GE                           { $$ = TK_GE; }
    | TK_GT                           { $$ = TK_GT; }
    | TK_EQ                           { $$ = TK_EQ; }
    | TK_NEQ                          { $$ = TK_NEQ; }
    | TK_AND                          { $$ = TK_AND; }
    | TK_OR                           { $$ = TK_OR; }
    ;

add_expr
    : add_expr TK_PLUS mul_expr       { $$ = mk_binop(TK_PLUS, $1, $3, @1.first_line); }
    | add_expr TK_MINUS mul_expr      { $$ = mk_binop(TK_MINUS, $1, $3, @1.first_line); }
    | mul_expr                        { $$ = $1; }
    ;

mul_expr
    : mul_expr TK_MUL primary         { $$ = mk_binop(TK_MUL, $1, $3, @1.first_line); }
    | mul_expr TK_DIV primary         { $$ = mk_binop(TK_DIV, $1, $3, @1.first_line); }
    | primary                         { $$ = $1; }
    ;

primary
    : TK_LPAREN expr TK_RPAREN        { $$ = $2; }
    | var                             { $$ = $1; }
    | TK_NUM                          { $$ = mk_num($1, @1.first_line); }
    | TK_MINUS primary                { $$ = mk_binop(TK_MINUS, mk_num(0, @1.first_line), $2, @1.first_line); }
    ;


%%

void yyerror(const char *s) {
    fprintf(stderr, "Parse error at line %d: %s\n",
            yylloc.first_line, s);
    if (++n_errors > 20) {
        fprintf(stderr, "Too many errors; aborting.\n");
        exit(1);
    }
}
