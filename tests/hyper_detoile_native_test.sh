#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$test_dir/.." && pwd)"
native_dir="${GOEMON64_RECOMP_DIR:-$repo_dir/../Goemon64Recomp}"
generated_dir="$native_dir/RecompiledFuncs"
runtime_include="$native_dir/lib/N64ModernRuntime/N64Recomp/include"

if [[ ! -d "$generated_dir" || ! -f "$runtime_include/recomp.h" ]]; then
    echo "SKIP: Goemon64Recomp generated functions/runtime headers not found at $native_dir"
    echo "      Set GOEMON64_RECOMP_DIR to run the optional native-backed smoke test."
    exit 0
fi
if ! command -v rg >/dev/null 2>&1; then
    echo "SKIP: rg is required to locate generated native functions."
    exit 0
fi

native_tmp="$(mktemp -d "${TMPDIR:-/tmp}/hyper-detoile-native.XXXXXX")"
cleanup() {
    rm -r -- "$native_tmp"
}
trap cleanup EXIT

extract_function() {
    local symbol="$1"
    local destination="$2"
    local source_file

    source_file="$(rg -l -uu "^RECOMP_FUNC void ${symbol}\\(" \
        "$generated_dir"/funcs_*.c)"
    if [[ -z "$source_file" || "$source_file" == *$'\n'* ]]; then
        echo "Expected exactly one generated definition for $symbol" >&2
        exit 1
    fi
    awk -v signature="RECOMP_FUNC void ${symbol}(" '
        index($0, signature) == 1 { copying = 1 }
        copying { print }
        copying && $0 == ";}" { found = 1; exit }
        END { if (!found) exit 1 }
    ' "$source_file" > "$destination"
}

extract_function func_801D2F40_5FE320 "$native_tmp/native_801D2F40.inc"
extract_function func_801D614C_60152C "$native_tmp/native_801D614C.inc"
extract_function func_801E4194_60F574 "$native_tmp/native_801E4194.inc"
extract_function func_801FCD1C_6280FC "$native_tmp/native_801FCD1C.inc"
extract_function func_801FD30C_6286EC "$native_tmp/native_801FD30C.inc"

native_cc="${NATIVE_TEST_CC:-cc}"
"$native_cc" -std=c11 -Wall -Wextra -Werror \
    -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unknown-pragmas \
    -fsanitize=undefined -fno-sanitize=shift \
    -I "$runtime_include" -I "$native_tmp" \
    "$test_dir/hyper_detoile_native_test.c" -lm \
    -o "$native_tmp/hyper_detoile_native_test"
"$native_tmp/hyper_detoile_native_test"
