#!/usr/bin/env bash
# 语义分析测试

cd "$(dirname "$0")/.."
BUILD=build

if [ ! -x "$BUILD/c1c_sema" ]; then
    echo "Building c1c_sema..."
    gcc -Wall -Wextra -O0 -g -I src/ast -I src/lexer -I src/sema \
      src/lexer/handwritten.c src/ast/ast.c src/sema/sema.c src/driver/main_sema.c \
      -o $BUILD/c1c_sema
fi

pass=0; fail=0
for t in tests/*.c1 examples/*.c1; do
    name=$(basename "$t")
    should_fail=""
    skip=""
    case "$name" in
        *sem_error*|*no_main*) should_fail=1 ;;
        *error_recovery*) skip=1 ;;
    esac

    if [ "$skip" = "1" ]; then
        echo "SKIP $name (parse fails, sema not applicable)"
        continue
    fi

    out=$(timeout 3 $BUILD/c1c_sema < "$t" 2>&1)
    rc=$?

    if [ -n "$should_fail" ]; then
        if [ "$rc" -ne 0 ]; then
            echo "OK   $name (semantic errors as expected)"
            pass=$((pass+1))
        else
            echo "FAIL $name (expected sema failure, got success)"
            fail=$((fail+1))
        fi
    else
        if [ "$rc" -eq 0 ]; then
            echo "OK   $name"
            pass=$((pass+1))
        else
            echo "FAIL $name"
            echo "$out"
            fail=$((fail+1))
        fi
    fi
done

echo
echo "Sema Result: $pass passed, $fail failed"
exit $fail
