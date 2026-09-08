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
    ./scripts/build.sh --full >/dev/null 2>&1
)

if ! grep -qx -- '--target' "${args_file}"; then
    echo "expected default target flag" >&2
    exit 1
fi

if ! grep -qx -- 'alientek-image-update' "${args_file}"; then
    echo "expected default target alientek-image-update" >&2
    exit 1
fi

echo "build.sh default target uses alientek-image-update"
