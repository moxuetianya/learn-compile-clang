/*
 * LLVM C++ API codegen —— C 兼容接口（第 9-10 章）
 *
 * 前端 (C: lexer/parser/sema) 通过这个接口调用后端 (C++: LLVM)。
 * 两条产出路径:
 *   c1_llvm_emit_text   → LLVM IR 文本 (.ll)
 *   c1_llvm_emit_object → 目标文件 (.o), 再由驱动链接成可执行文件
 */

#ifndef C1C_LLVM_CODEGEN_H
#define C1C_LLVM_CODEGEN_H

#include "ast.h"

/* optimize: 0 = 不优化 (clang -O0 风格), 1 = mem2reg + instcombine 等 (第 11 章) */
int c1_llvm_emit_text(ASTNode *root, FILE *out, int optimize);
int c1_llvm_emit_object(ASTNode *root, const char *obj_path, int optimize);

#endif
