#!/usr/bin/env bash
# 扩展测试: 第 11 章 Pass 插件 / 第 12 章 Clang 插件 / 第 13 章 ORC JIT
# 各节独立, 对应工具未构建则跳过该节。
set -u

cd "$(dirname "$0")/.."
BUILD=build
OPT=${OPT:-opt-18}
CLANG=${CLANG:-clang-18}
LLC=${LLC:-llc-18}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0
ok()   { echo "  ✓ $1"; pass=$((pass+1)); }
bad()  { echo "  ✗ $1"; fail=$((fail+1)); }

# ========== A. constfold Pass 插件（第 11 章）==========
if [ -f "$BUILD/constfold.so" ]; then
    echo "=== [constfold pass] 13_constfold ==="
    "$BUILD/c1c_emit" < tests/13_constfold.c1 > "$TMP/fold.ll" 2>/dev/null

    # 我们的 pass 和 LLVM 自带 pass 组合成流水线。
    # 注意顺序与迭代: mem2reg 先把变量提升成 phi; constfold 折掉
    # 常量运算和 icmp; simplifycfg 消掉 'br i1 true' 的死分支后,
    # phi 又退化成常量 —— 需要 再跑一次 constfold 才能折到底。
    # (真实世界里 -O2 就是把 pass 交替跑到不动点, 原因就在这)
    if "$OPT" -load-pass-plugin="$BUILD/constfold.so" \
              -passes='function(mem2reg,c1c-constfold,simplifycfg,c1c-constfold)' \
              "$TMP/fold.ll" -S -o "$TMP/folded.ll" 2>"$TMP/fold.err"; then
        # 一路折到底: main 只剩 ret i32 25
        if grep -q 'ret i32 25' "$TMP/folded.ll" && \
           ! grep -qE 'add i32|sub i32|mul i32|icmp|phi' "$TMP/folded.ll"; then
            ok "全部常量折叠: main 只剩 ret i32 25"
        else
            bad "折叠不完全:"; grep -E 'add|sub|mul|icmp|ret' "$TMP/folded.ll" | head -5
        fi
    else
        bad "opt 加载插件失败:"; head -5 "$TMP/fold.err"
    fi

    # 折叠后的程序行为必须不变: 编译运行比对退出码
    want=$(cat tests/e2e/13_constfold.exit)
    if "$LLC" "$TMP/folded.ll" -o "$TMP/folded.s" && \
       "$CLANG" "$TMP/folded.s" -o "$TMP/folded"; then
        "$TMP/folded"; got=$?
        [ "$got" = "$want" ] && ok "折叠后运行 exit $got" \
                              || bad "折叠后运行 exit $got, want $want"
    else
        bad "折叠后 IR 无法编译"
    fi
fi

# ========== B. Clang 插件（第 12 章）==========
if [ -f "$BUILD/unused-vars.so" ]; then
    echo "=== [clang plugin] unused-vars ==="
    "$CLANG" -fplugin="$BUILD/unused-vars.so" -fsyntax-only \
             tests/clang_plugin_demo.c 2> "$TMP/plugin.err"
    n=$(grep -c "unused variable" "$TMP/plugin.err" || true)
    if [ "$n" = "2" ] && grep -q "unused variable 'unused_var'" "$TMP/plugin.err" \
                      && grep -q "unused variable 'also_unused'" "$TMP/plugin.err"; then
        ok "准确报告 2 个未使用变量 (unused_var, also_unused)"
    else
        bad "期望 2 个告警, 实际 $n 个:"; cat "$TMP/plugin.err"
    fi
fi

# ========== C. ORC JIT（第 13 章）==========
if [ -x "$BUILD/c1c_jit" ]; then
    for t in examples/*.c1 tests/*.c1; do
        name=$(basename "$t" .c1)
        exp="tests/e2e/${name}.exit"
        [ -f "$exp" ] || continue
        want=$(cat "$exp")
        echo "=== [jit] $name (expect $want) ==="
        out=$("$BUILD/c1c_jit" "$t" 2>/dev/null | grep -oE '[0-9]+$')
        [ "$out" = "$want" ] && ok "jit main returned $out" \
                              || bad "jit returned ${out:-<none>}, want $want"
        out=$("$BUILD/c1c_jit" "$t" --opt 2>/dev/null | grep -oE '[0-9]+$')
        [ "$out" = "$want" ] && ok "jit --opt main returned $out" \
                              || bad "jit --opt returned ${out:-<none>}, want $want"
    done
fi

echo
echo "Extras Result: $pass passed, $fail failed"
exit $fail
