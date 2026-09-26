#!/usr/bin/env bash
# 跑全部测试 — 同时验证手写版本和 bison 版本（如果有）
set -e

cd "$(dirname "$0")/.."
BUILD=build

if [ ! -x "$BUILD/c1c_hand" ]; then
    echo "Building c1c_hand..."
    make c1c_hand
fi
if [ ! -x "$BUILD/c1c" ] && which bison >/dev/null 2>&1 && which flex >/dev/null 2>&1; then
    echo "Building c1c..."
    make c1c || echo "  (c1c build failed; using c1c_hand only)"
fi

run_test() {
    local bin=$1
    local t=$2
    local name=$3
    local should_fail=""
    # 期望解析失败的: 名字含 error_recovery / invalid / parse_error
    case "$name" in
        *error_recovery*|*parse_error*|*invalid*) should_fail=1 ;;
    esac

    echo "=== [$bin] $name ==="
    if "$bin" --dump-ast < "$t" > /tmp/out 2> /tmp/err; then
        if [ -n "$should_fail" ]; then
            echo "  ✗ expected parse failure, but parsed OK"
            return 1
        fi
        if [ -f "tests/expected/${name}.txt" ]; then
            if diff -u "tests/expected/${name}.txt" /tmp/out > /dev/null; then
                echo "  ✓ AST matches expected"
                return 0
            else
                echo "  ✗ mismatch"
                diff -u "tests/expected/${name}.txt" /tmp/out | head -20
                return 1
            fi
        else
            echo "  ✓ parsed (no expected file)"
            return 0
        fi
    else
        if [ -n "$should_fail" ]; then
            echo "  ✓ parse failed as expected"
            return 0
        fi
        echo "  ✗ parse failed:"
        cat /tmp/err
        return 1
    fi
}

pass=0; fail=0
for t in tests/*.c1 examples/*.c1; do
    name=$(basename "$t")
    ok=1
    run_test "$BUILD/c1c_hand" "$t" "$name" || ok=0
    if [ -x "$BUILD/c1c" ]; then
        run_test "$BUILD/c1c" "$t" "$name" > /tmp/log2 2>&1 || ok=0
        cat /tmp/log2
    fi
    if [ "$ok" = "1" ]; then pass=$((pass+1)); else fail=$((fail+1)); fi
    echo
done

echo "Result: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
