#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$test_dir/.." && pwd)"
native_dir="${GOEMON64_RECOMP_DIR:-$repo_dir/../Goemon64Recomp}"
generated_dir="$native_dir/RecompiledFuncs"
runtime_include="$native_dir/lib/N64ModernRuntime/N64Recomp/include"

if [[ ! -d "$generated_dir" || ! -f "$runtime_include/recomp.h" ]]; then
    echo "SKIP: Goemon64Recomp generated functions/runtime headers not found at $native_dir"
    echo "      Set GOEMON64_RECOMP_DIR to run the optional native-backed regression."
    exit 0
fi
if ! command -v rg >/dev/null 2>&1; then
    echo "SKIP: rg is required to locate generated native functions."
    exit 0
fi

native_tmp="$(mktemp -d "${TMPDIR:-/tmp}/debug-balberra-native.XXXXXX")"
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

extract_function func_801D36CC_5FEAAC "$native_tmp/native_801D36CC.inc"
extract_function func_801D3894_5FEC74 "$native_tmp/native_801D3894.inc"
extract_function func_8020407C_62F45C "$native_tmp/native_8020407C.inc"
extract_function func_8020451C_62F8FC "$native_tmp/native_8020451C.inc"
extract_function func_802045E8_62F9C8 "$native_tmp/native_802045E8.inc"
extract_function func_802045F4_62F9D4 "$native_tmp/native_802045F4.inc"
extract_function func_80204600_62F9E0 "$native_tmp/native_80204600.inc"

native_cc="${NATIVE_TEST_CC:-cc}"
common_flags=(
    -std=c11 -Wall -Wextra -Werror
    -Wno-unused-variable -Wno-unused-but-set-variable
    -Wno-unknown-pragmas -fsanitize=undefined -fno-sanitize=shift
    -I "$repo_dir/include" -I "$runtime_include" -I "$native_tmp"
)

for debug_value in 0 1; do
    output="$native_tmp/debug_balberra_native_$debug_value"
    "$native_cc" "${common_flags[@]}" -DEXTRA_OPTIONS_DEBUG="$debug_value" \
        "$test_dir/debug_balberra_native_test.c" -lm -o "$output"
    "$output"
done
