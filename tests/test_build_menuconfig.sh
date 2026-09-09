#!/bin/sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
tmpdir="$(mktemp -d)"
mockbin="${tmpdir}/bin"
mkdir -p "${mockbin}"
trap 'rm -rf "${tmpdir}"' EXIT INT TERM

cat > "${mockbin}/kas" <<'EOF'
#!/bin/sh
printf '%s\n' "$@" > "${BUILD_SH_ARGS_FILE:?}"
EOF
chmod +x "${mockbin}/kas"

args_file="${tmpdir}/kas-args.txt"
(
    cd "${repo_root}"
    PATH="${mockbin}:$PATH" \
    KAS_USE_HOST=1 \
    BUILD_SH_ARGS_FILE="${args_file}" \
    ./scripts/build.sh --menuconfig >/dev/null 2>&1
)

grep -qx -- 'shell' "${args_file}"
grep -qx -- "${repo_root}/kas/alientek-alpha.yml" "${args_file}"
grep -qx -- '-c' "${args_file}"
grep -q 'bitbake virtual/kernel -c menuconfig' "${args_file}"
grep -q 'bitbake -c diffconfig virtual/kernel' "${args_file}"
grep -q 'TOPDIR=' "${args_file}"
grep -q '${topdir}/menuconfig/kernel.fragment.cfg' "${args_file}"

if grep -qx -- '--target' "${args_file}"; then
    echo "unexpected default target flag for menuconfig" >&2
    exit 1
fi

if grep -q '\$\(pwd\)/build/menuconfig' "${args_file}"; then
    echo "unexpected pwd-based nested build/menuconfig export path" >&2
    exit 1
fi

echo "build.sh menuconfig uses kernel shell flow"

set +e
(
    cd "${repo_root}"
    PATH="${mockbin}:$PATH" \
    KAS_USE_HOST=1 \
    ./scripts/build.sh --menuconfig --fetch-only >/dev/null 2>"${tmpdir}/conflict.err"
)
status="$?"
set -e

if [ "${status}" -eq 0 ]; then
    echo "expected --menuconfig to reject --fetch-only" >&2
    exit 1
fi

grep -q -- '--menuconfig 不能与 --fetch-only 同用' "${tmpdir}/conflict.err"

echo "build.sh menuconfig rejects conflicting flags"
