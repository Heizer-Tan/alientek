#!/bin/sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
tmpdir="$(mktemp -d)"
project_root="${tmpdir}/project"
mockbin="${tmpdir}/bin"
mkdir -p "${project_root}/scripts" "${project_root}/kas" "${mockbin}"
trap 'rm -rf "${tmpdir}"' EXIT INT TERM

cp "${repo_root}/scripts/build.sh" "${project_root}/scripts/build.sh"
cp "${repo_root}/scripts/kas-env.sh" "${project_root}/scripts/kas-env.sh"
chmod +x "${project_root}/scripts/build.sh"

cat > "${project_root}/scripts/build-dtb.sh" <<'EOF'
#!/bin/sh
printf 'build-dtb\n' > "${BUILD_DTB_CALLED_FILE:?}"
EOF
chmod +x "${project_root}/scripts/build-dtb.sh"

cat > "${mockbin}/kas" <<'EOF'
#!/bin/sh
printf 'kas\n' > "${KAS_CALLED_FILE:?}"
EOF
chmod +x "${mockbin}/kas"

mkdir -p \
    "${project_root}/meta-alientek/recipes-bsp/device-tree/alientek-aes" \
    "${project_root}/build/tmp/deploy/images/imx6ull-alientek-alpha"

cat > "${project_root}/meta-alientek/recipes-bsp/device-tree/alientek-aes/imx6ull-alientek-alpha.dtsi" <<'EOF'
/dts-v1/;
EOF

printf 'dtb\n' > "${project_root}/build/tmp/deploy/images/imx6ull-alientek-alpha/imx6ull-alientek-alpha.dtb"
touch -t 202001010000 "${project_root}/build/tmp/deploy/images/imx6ull-alientek-alpha/imx6ull-alientek-alpha.dtb"
touch -t 202001010100 "${project_root}/meta-alientek/recipes-bsp/device-tree/alientek-aes/imx6ull-alientek-alpha.dtsi"
printf 'header: {}\n' > "${project_root}/kas/alientek-alpha.yml"

build_dtb_called="${tmpdir}/build-dtb-called.txt"
kas_called="${tmpdir}/kas-called.txt"

(
    cd "${project_root}"
    PATH="${mockbin}:$PATH" \
    KAS_USE_HOST=1 \
    BUILD_DTB_CALLED_FILE="${build_dtb_called}" \
    KAS_CALLED_FILE="${kas_called}" \
    ./scripts/build.sh >/dev/null 2>&1
)

if [ ! -f "${build_dtb_called}" ]; then
    echo "expected non-git device tree change to use build-dtb.sh" >&2
    exit 1
fi

if [ -f "${kas_called}" ]; then
    echo "unexpected kas invocation for non-git device tree fast path" >&2
    exit 1
fi

echo "build.sh uses build-dtb.sh for non-git device tree changes"
