#!/usr/bin/env bash
# 一条龙: .c1 --c1c_emit--> .ll --llc--> .s --clang--> 可执行文件 → 运行
# 用法: scripts/build_and_run.sh [源文件] [输出名]
set -e
cd "$(dirname "$0")/.."

SRC=${1:-examples/fib.c1}
OUT=${2:-build/out}

LLC=$(command -v llc-18 || command -v llc)
CLANG=$(command -v clang-18 || command -v clang)
OPT=$(command -v opt-18 || command -v opt)

[ -x build/c1c_emit ] || make c1c_emit

# 1) 前端: 词法/语法/语义 + IR 文本
./build/c1c_emit < "$SRC" > "$OUT.ll"
echo "== $OUT.ll (LLVM IR) =="

# 2) IR 验证器
if [ -n "$OPT" ]; then "$OPT" -passes=verify -disable-output "$OUT.ll"; fi

# 3) 中端+后端: llc → 汇编
"$LLC" "$OUT.ll" -o "$OUT.s"
echo "== $OUT.s (汇编, 前 12 行) =="
head -12 "$OUT.s"

# 4) 链接
"$CLANG" "$OUT.s" -o "$OUT"

# 5) 运行 (注意: 程序退出码非 0 不能让 set -e 杀掉脚本)
echo "== run $OUT =="
rc=0; "$OUT" || rc=$?
echo "exit=$rc"
