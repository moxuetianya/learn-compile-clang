# c1c — C1 子集编译器
#
# 当前阶段: 词法 + 语法 + AST + 语义分析
# 后续会加: LLVM IR 生成
#
# 两条构建路径:
#   1) make c1c       用 flex/bison (推荐, 工业路线)
#   2) make c1c_hand  用纯 C 手写 (不依赖 flex/bison, 任何环境能跑)

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O0 -g

LLVM_CONFIG ?= llvm-config-18
ifeq ($(shell which $(LLVM_CONFIG) 2>/dev/null),)
  LLVM_CONFIG := llvm-config
endif
LLVM_CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags 2>/dev/null)

# bison/flex 可选 (项目附了生成的 .c/.h 时不需要)
BISON   ?= bison
FLEX    ?= flex

# bison 需要 m4sugar.m4, 默认从 /usr/share/bison 找
# 如果装在非标准位置 (例如 dpkg -x 解压), 需要指定 PKGDATADIR
BISON_PKGDATADIR ?=
ifneq ($(shell which $(BISON) 2>/dev/null),)
  ifeq ($(shell ls /usr/share/bison/m4sugar/m4sugar.m4 2>/dev/null),)
    BISON_PKGDATADIR := $(shell dirname $(shell readlink -f $(shell which $(BISON))))/../share/bison
  endif
endif

BUILD   := build

# 端到端工具链 (自动探测带版本号的 llc/clang)
LLC     ?= $(firstword $(shell which llc-18 llc 2>/dev/null))
CLANG   ?= $(firstword $(shell which clang-18 clang 2>/dev/null))
OPT     ?= $(firstword $(shell which opt-18 opt 2>/dev/null))

# LLVM C++ 后端 (第 9-10 章): 需要 llvm-config
LLVM_OK := $(shell which $(LLVM_CONFIG) 2>/dev/null)
CXX     ?= clang++

.PHONY: all c1c c1c_hand c1c_sema c1c_emit c1c_full lexer-test parser-test clean test bison-report

all: c1c_hand c1c_sema c1c c1c_emit c1c_full

# ---------- 手写版本（无外部依赖）----------
c1c_hand: src/lexer/handwritten.c src/ast/ast.c src/driver/main_hand.c | $(BUILD)
	$(CC) $(CFLAGS) -I src/ast -I src/lexer \
	  src/lexer/handwritten.c src/ast/ast.c src/driver/main_hand.c \
	  -o $(BUILD)/c1c_hand

# ---------- 语义分析版本 ----------
c1c_sema: src/lexer/handwritten.c src/ast/ast.c src/sema/sema.c src/driver/main_sema.c | $(BUILD)
	$(CC) $(CFLAGS) -I src/ast -I src/lexer -I src/sema \
	  src/lexer/handwritten.c src/ast/ast.c src/sema/sema.c src/driver/main_sema.c \
	  -o $(BUILD)/c1c_sema

# ---------- flex/bison 版本（推荐）----------
c1c:
	@if which $(BISON) >/dev/null 2>&1 && which $(FLEX) >/dev/null 2>&1; then \
	    $(MAKE) $(BUILD)/lex.yy.c $(BUILD)/c1.tab.c $(BUILD)/c1.tab.h; \
	    $(CC) $(CFLAGS) -I src/ast -I src/lexer -I $(BUILD) \
	      $(BUILD)/lex.yy.c $(BUILD)/c1.tab.c \
	      src/ast/ast.c src/driver/main.c \
	      -o $(BUILD)/c1c; \
	else \
	    echo "Skipping c1c: flex+bison not installed (use 'apt install flex bison' or use c1c_hand)"; \
	fi

# ---------- 文本 IR 生成版本（第 9 章）----------
c1c_emit: src/lexer/handwritten.c src/ast/ast.c src/sema/sema.c \
          src/codegen/codegen.c src/driver/main_emit.c | $(BUILD)
	$(CC) $(CFLAGS) -I src/ast -I src/lexer -I src/sema -I src/codegen \
	  src/lexer/handwritten.c src/ast/ast.c src/sema/sema.c \
	  src/codegen/codegen.c src/driver/main_emit.c \
	  -o $(BUILD)/c1c_emit

# ---------- LLVM C++ API 完整版（第 9-10 章）----------
# 前端 C (handwritten/ast/sema) + 后端 C++ (LLVM IRBuilder → .o) + 链接
c1c_full:
	@if [ -n "$(LLVM_OK)" ]; then \
	    $(MAKE) $(BUILD)/c1c_full; \
	else \
	    echo "Skipping c1c_full: llvm-config not found (sudo apt install llvm-18-dev)"; \
	fi

$(BUILD)/c1c_full: src/lexer/handwritten.c src/ast/ast.c src/sema/sema.c \
                   src/codegen/llvm_codegen.cpp src/driver/main_full.cpp \
                   src/codegen/llvm_codegen.h | $(BUILD)
	$(CC) $(CFLAGS) -I src/ast -I src/lexer -c src/lexer/handwritten.c -o $(BUILD)/handwritten.o
	$(CC) $(CFLAGS) -I src/ast -I src/lexer -c src/ast/ast.c -o $(BUILD)/ast.o
	$(CC) $(CFLAGS) -I src/ast -I src/sema -c src/sema/sema.c -o $(BUILD)/sema.o
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -I src -I src/ast -I src/lexer -I src/sema -I src/codegen \
	  -c src/codegen/llvm_codegen.cpp -o $(BUILD)/llvm_codegen.o
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -I src -I src/ast -I src/lexer -I src/sema -I src/codegen \
	  -c src/driver/main_full.cpp -o $(BUILD)/main_full.o
	$(CXX) $(BUILD)/handwritten.o $(BUILD)/ast.o $(BUILD)/sema.o \
	  $(BUILD)/llvm_codegen.o $(BUILD)/main_full.o \
	  $(shell $(LLVM_CONFIG) --libs core native) $(shell $(LLVM_CONFIG) --system-libs) \
	  -o $(BUILD)/c1c_full

CXXFLAGS ?= -Wall -Wextra -O0 -g

# ---------- 单独词法测试 ----------
lexer-test: $(BUILD)/lex.yy.c $(BUILD)/c1.tab.c $(BUILD)/c1.tab.h src/lexer/main.c
	$(CC) $(CFLAGS) -I src/lexer -I $(BUILD) \
	  $(BUILD)/lex.yy.c $(BUILD)/c1.tab.c \
	  src/lexer/main.c -o $(BUILD)/lexer_test

# ---------- 单独语法测试 ----------
parser-test: $(BUILD)/lex.yy.c $(BUILD)/c1.tab.c $(BUILD)/c1.tab.h src/ast/ast.c
	$(CC) $(CFLAGS) -I src/ast -I src/lexer -I $(BUILD) \
	  $(BUILD)/lex.yy.c $(BUILD)/c1.tab.c \
	  src/ast/ast.c -o $(BUILD)/parser_test

# ---------- 生成 bison/flex 输出 ----------
$(BUILD)/c1.tab.c $(BUILD)/c1.tab.h: src/parser/c1.y | $(BUILD)
	$(if $(BISON_PKGDATADIR),BISON_PKGDATADIR=$(BISON_PKGDATADIR)) $(BISON) -d -o $(BUILD)/c1.tab.c src/parser/c1.y

$(BUILD)/lex.yy.c: src/lexer/c1.l $(BUILD)/c1.tab.h | $(BUILD)
	$(FLEX) -o $(BUILD)/lex.yy.c src/lexer/c1.l

# 调试 bison 状态机 (生成 .output + .dot)
bison-report: | $(BUILD)
	$(if $(BISON_PKGDATADIR),BISON_PKGDATADIR=$(BISON_PKGDATADIR)) $(BISON) -d --report=all --graph=$(BUILD)/parser.dot -o $(BUILD)/c1.tab.c src/parser/c1.y
	@echo "Generated: $(BUILD)/c1.output and $(BUILD)/parser.dot"
	@which dot >/dev/null 2>&1 && dot -Tpng $(BUILD)/parser.dot -o $(BUILD)/parser.png && echo "Also: $(BUILD)/parser.png" || true

$(BUILD):
	mkdir -p $(BUILD)

# ---------- 测试 ----------
test: c1c_hand c1c_sema c1c c1c_emit
	@bash scripts/run_tests.sh
	@bash scripts/run_sema_tests.sh
	@if [ -n "$(LLC)" ] && [ -n "$(CLANG)" ]; then \
	    LLC=$(LLC) CLANG=$(CLANG) OPT=$(OPT) bash scripts/run_e2e_tests.sh; \
	else \
	    echo "(llc/clang 不可用, 跳过端到端测试)"; \
	fi

clean:
	rm -rf $(BUILD)
