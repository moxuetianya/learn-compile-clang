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

/* JIT 用 (第 13 章): 构建模块并移交所有权。
 * out_ctx/out_mod 是不透明的指针, 实际类型是 LLVMContext 与 Module,
 * 调用方负责用对应的智能指针接管并 delete。 */
int c1_llvm_build_module(ASTNode *root, int optimize,
                         void **out_ctx, void **out_mod);

#endif
