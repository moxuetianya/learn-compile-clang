#!/usr/bin/env bash
# 端到端测试: .c1 --c1c_emit--> .ll --llc--> .s --clang--> 可执行文件
# 退出码与 tests/e2e/<name>.exit 中的期望值比对。
#
# 流水线:
#   c1c_emit   前端 + IR 文本生成 (第 9 章)
#   opt -verify  IR 验证器: 检查非法 IR (块没终结/终结后还有指令/类型错误)
#   llc        IR → 目标代码 (第 10 章)
#   clang      链接成可执行文件
set -u

cd "$(dirname "$0")/.."
BUILD=build
LLC=${LLC:-llc-18}
CLANG=${CLANG:-clang-18}
OPT=${OPT:-opt-18}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0

for t in examples/*.c1 tests/*.c1; do
    name=$(basename "$t" .c1)
    exp="tests/e2e/${name}.exit"
    [ -f "$exp" ] || continue
    want=$(cat "$exp")

    echo "=== E2E $name (expect exit $want) ==="

    # 1) 前端 + IR 生成
    if ! "$BUILD/c1c_emit" < "$t" > "$TMP/$name.ll" 2> "$TMP/$name.err"; then
        echo "  ✗ c1c_emit failed"; cat "$TMP/$name.err"; fail=$((fail+1)); continue
    fi

    # 2) IR 验证器 (教学价值: 非法 IR 在这里现形)
    if [ -n "$OPT" ] && [ "$(command -v "$OPT")" ]; then
        if ! "$OPT" -passes=verify -disable-output "$TMP/$name.ll" 2> "$TMP/$name.verr"; then
            echo "  ✗ IR verify failed:"; head -10 "$TMP/$name.verr"
            fail=$((fail+1)); continue
        fi
    fi

    # 3) 汇编 + 链接 + 运行
    if ! "$LLC" "$TMP/$name.ll" -o "$TMP/$name.s" 2> "$TMP/$name.lerr"; then
        echo "  ✗ llc failed:"; head -10 "$TMP/$name.lerr"; fail=$((fail+1)); continue
    fi
    if ! "$CLANG" "$TMP/$name.s" -o "$TMP/$name" 2> "$TMP/$name.cerr"; then
        echo "  ✗ clang link failed:"; head -10 "$TMP/$name.cerr"; fail=$((fail+1)); continue
    fi
    "$TMP/$name"
    got=$?

    if [ "$got" = "$want" ]; then
        echo "  ✓ exit $got"
        pass=$((pass+1))
    else
        echo "  ✗ exit $got, want $want"
        fail=$((fail+1))
    fi
done

echo
echo "E2E Result: $pass passed, $fail failed"
exit $fail
