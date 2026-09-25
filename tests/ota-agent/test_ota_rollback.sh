#!/bin/sh
# 宿主机可跑的回滚/提交逻辑冒烟：mock fw_* 与 reboot，验证健康检查失败会回滚

set -eu

repo_root="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT INT TERM

mockbin="${tmpdir}/bin"
mkdir -p "${mockbin}" "${tmpdir}/etc/default" "${tmpdir}/share" "${tmpdir}/sbin"

# 模拟 U-Boot 环境
cat > "${tmpdir}/env" <<'EOF'
active_slot=B
upgrade_available=1
bootcount=1
last_good_slot=A
EOF

cat > "${mockbin}/fw_printenv" <<EOF
#!/bin/sh
envfile="${tmpdir}/env"
if [ "\$1" = "-n" ]; then
  key="\$2"
  grep "^\${key}=" "\$envfile" | sed "s/^[^=]*=//" || exit 1
  exit 0
fi
cat "\$envfile"
EOF
chmod +x "${mockbin}/fw_printenv"

cat > "${mockbin}/fw_setenv" <<EOF
#!/bin/sh
envfile="${tmpdir}/env"
key="\$1"
val="\$2"
tmp="\${envfile}.tmp"
grep -v "^\${key}=" "\$envfile" > "\$tmp" || true
printf '%s=%s\n' "\$key" "\$val" >> "\$tmp"
mv "\$tmp" "\$envfile"
EOF
chmod +x "${mockbin}/fw_setenv"

cat > "${mockbin}/logger" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "${mockbin}/logger"

reboot_flag="${tmpdir}/rebooted"
cat > "${mockbin}/reboot" <<EOF
#!/bin/sh
touch "${reboot_flag}"
EOF
chmod +x "${mockbin}/reboot"

# 健康检查故意失败
cat > "${tmpdir}/sbin/board-upgrade-healthcheck" <<'EOF'
#!/bin/sh
exit 1
EOF
chmod +x "${tmpdir}/sbin/board-upgrade-healthcheck"

cat > "${tmpdir}/etc/default/board-upgrade-commit" <<EOF
BOARD_UPGRADE_HEALTH_ENABLE=1
BOARD_UPGRADE_HEALTH_CMD=${tmpdir}/sbin/board-upgrade-healthcheck
BOARD_UPGRADE_ROLLBACK_ON_FAIL=1
EOF

cp "${repo_root}/meta-alientek/recipes-core/ota/board-update-tools/files/board-upgrade-commit.init" \
    "${tmpdir}/board-upgrade-commit"
# 替换 defaults 路径注入
sed -i "s|/etc/default/board-upgrade-commit|${tmpdir}/etc/default/board-upgrade-commit|" \
    "${tmpdir}/board-upgrade-commit"

PATH="${mockbin}:$PATH" \
BOARD_UPGRADE_COMMIT_DEFAULTS="${tmpdir}/etc/default/board-upgrade-commit" \
BOARD_UPGRADE_SKIP_SYNC=1 \
    sh "${tmpdir}/board-upgrade-commit" start || true

grep -q '^active_slot=A$' "${tmpdir}/env" || {
    echo "expected rollback to active_slot=A" >&2
    cat "${tmpdir}/env" >&2
    exit 1
}
grep -q '^upgrade_available=0$' "${tmpdir}/env" || {
    echo "expected upgrade_available=0 after rollback" >&2
    exit 1
}
test -f "${reboot_flag}" || {
    echo "expected reboot after health-check failure" >&2
    exit 1
}

# 健康检查通过应提交
rm -f "${reboot_flag}"
cat > "${tmpdir}/env" <<'EOF'
active_slot=B
upgrade_available=1
bootcount=1
last_good_slot=A
EOF
cat > "${tmpdir}/sbin/board-upgrade-healthcheck" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "${tmpdir}/sbin/board-upgrade-healthcheck"

PATH="${mockbin}:$PATH" \
BOARD_UPGRADE_COMMIT_DEFAULTS="${tmpdir}/etc/default/board-upgrade-commit" \
BOARD_UPGRADE_SKIP_SYNC=1 \
    sh "${tmpdir}/board-upgrade-commit" start

grep -q '^upgrade_available=0$' "${tmpdir}/env" || {
    echo "expected commit clears upgrade_available" >&2
    exit 1
}
grep -q '^last_good_slot=B$' "${tmpdir}/env" || {
    echo "expected last_good_slot=B" >&2
    exit 1
}
grep -q '^ota_phase=committed$' "${tmpdir}/env" || {
    echo "expected ota_phase=committed" >&2
    exit 1
}
test ! -f "${reboot_flag}" || {
    echo "commit path must not reboot" >&2
    exit 1
}

# 槽位库：PARTUUID 识别
cp "${repo_root}/meta-alientek/recipes-core/ota/board-update-tools/files/board-slot-lib.sh" \
    "${tmpdir}/share/board-slot-lib.sh"
cat > "${tmpdir}/cmdline" <<'EOF'
console=ttymxc0,115200 root=PARTUUID=deadbeef-03 rootwait rw
EOF
cat > "${tmpdir}/env" <<'EOF'
active_slot=B
upgrade_available=0
rootfs_a_partuuid=deadbeef-02
rootfs_b_partuuid=deadbeef-03
EOF
export PATH="${mockbin}:$PATH"
# shellcheck disable=SC1090
BOARD_CMDLINE_FILE="${tmpdir}/cmdline" . "${tmpdir}/share/board-slot-lib.sh"
slot="$(board_read_current_slot)"
test "${slot}" = "B" || {
    echo "PARTUUID should map to slot B, got ${slot}" >&2
    exit 1
}

echo "ota rollback/commit + PARTUUID slot detection smoke ok"
