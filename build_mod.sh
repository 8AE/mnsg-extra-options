#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

make_args=()

mod_filename="$(awk -F'"' '/^[[:space:]]*mod_filename[[:space:]]*=/ { print $2; exit }' mod.toml)"
if [[ -z "$mod_filename" ]]; then
    echo "Error: could not read inputs.mod_filename from mod.toml." >&2
    exit 1
fi
if [[ ! "$mod_filename" =~ ^[a-zA-Z0-9_.-]+$ ]]; then
    echo "Error: mod_filename must be a plain filename without directory components." >&2
    exit 1
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
    if [[ -z "${CC:-}" ]]; then
        for compiler in \
            /opt/homebrew/opt/llvm/bin/clang \
            /usr/local/opt/llvm/bin/clang
        do
            if [[ -x "$compiler" ]]; then
                CC="$compiler"
                break
            fi
        done
    fi

    if [[ -z "${LD:-}" ]]; then
        for linker in \
            /opt/homebrew/opt/lld/bin/ld.lld \
            /usr/local/opt/lld/bin/ld.lld \
            /opt/homebrew/opt/llvm/bin/ld.lld \
            /usr/local/opt/llvm/bin/ld.lld
        do
            if [[ -x "$linker" ]]; then
                LD="$linker"
                break
            fi
        done
    fi

    if [[ -z "${CC:-}" || -z "${LD:-}" ]]; then
        echo "Error: Homebrew LLVM and LLD are required on macOS." >&2
        echo "Install them with: brew install llvm lld" >&2
        exit 1
    fi
fi

if [[ -n "${CC:-}" ]]; then
    make_args+=("CC=$CC")
fi
if [[ -n "${LD:-}" ]]; then
    make_args+=("LD=$LD")
fi

if [[ -n "${RECOMP_MOD_TOOL:-}" ]]; then
    mod_tool="$RECOMP_MOD_TOOL"
elif [[ -x "$SCRIPT_DIR/RecompModTool" ]]; then
    mod_tool="$SCRIPT_DIR/RecompModTool"
elif [[ -x "$SCRIPT_DIR/../mnsg-recomp-example/RecompModTool" ]]; then
    mod_tool="$SCRIPT_DIR/../mnsg-recomp-example/RecompModTool"
elif command -v RecompModTool >/dev/null 2>&1; then
    mod_tool="$(command -v RecompModTool)"
else
    echo "Error: RecompModTool was not found." >&2
    echo "Set RECOMP_MOD_TOOL or place RecompModTool in this repository." >&2
    exit 1
fi

if [[ ! -x "$mod_tool" ]]; then
    echo "Error: RecompModTool is not executable: $mod_tool" >&2
    exit 1
fi

package_path="build/${mod_filename}.nrm"
debug_package_path="build/debug_${mod_filename}.nrm"
package_temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/mnsg-extra-options-build.XXXXXX")"
cleanup_packages() {
    rm -f -- "$package_temp_dir/debug.nrm"
    rmdir -- "$package_temp_dir"
}
trap cleanup_packages EXIT

build_variant() {
    local debug_flag="$1"
    shift
    make clean
    # The script owns the variant flag; caller make arguments cannot cause
    # the normal and debug filenames to accidentally receive the same build.
    make "${make_args[@]}" "$@" "EXTRA_OPTIONS_DEBUG=$debug_flag"
    "$mod_tool" mod.toml build
    if [[ ! -f "$package_path" ]]; then
        echo "Error: RecompModTool did not create $package_path." >&2
        exit 1
    fi
}

# Stage debug outside build/ because the normal build cleans that directory.
# Build normal last so build/mod.elf and subsequent plain make runs are safe
# release defaults. Both packages retain the same mod ID/configuration.
build_variant 1 "$@"
mv -- "$package_path" "$package_temp_dir/debug.nrm"
build_variant 0 "$@"
mv -- "$package_temp_dir/debug.nrm" "$debug_package_path"

echo "Done: $package_path"
echo "Done: $debug_package_path"
