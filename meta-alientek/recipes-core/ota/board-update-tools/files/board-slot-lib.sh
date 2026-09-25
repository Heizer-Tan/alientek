#!/bin/sh
# 槽位辅助库：供 board-apply-update 等脚本 source
# 识别 root=/dev/mmcblk0p{2,3}、PARTLABEL、PARTUUID（环境变量 rootfs_a_partuuid / rootfs_b_partuuid）

BOARD_CMDLINE_FILE="${BOARD_CMDLINE_FILE:-/proc/cmdline}"

board_read_active_slot() {
    fw_printenv -n active_slot 2>/dev/null || printf '%s\n' "A"
}

board_read_upgrade_available() {
    fw_printenv -n upgrade_available 2>/dev/null || printf '%s\n' "0"
}

board_read_partuuid_env() {
    slot="$1"
    case "${slot}" in
        A) fw_printenv -n rootfs_a_partuuid 2>/dev/null || true ;;
        B) fw_printenv -n rootfs_b_partuuid 2>/dev/null || true ;;
    esac
}

board_read_current_slot() {
    cmdline_file="${BOARD_CMDLINE_FILE}"
    if grep -Eq '(^| )root=/dev/mmcblk0p2( |$)' "${cmdline_file}"; then
        printf '%s\n' "A"
        return 0
    fi
    if grep -Eq '(^| )root=/dev/mmcblk0p3( |$)' "${cmdline_file}"; then
        printf '%s\n' "B"
        return 0
    fi
    if grep -Eq '(^| )root=PARTLABEL=rootfsA( |$)' "${cmdline_file}"; then
        printf '%s\n' "A"
        return 0
    fi
    if grep -Eq '(^| )root=PARTLABEL=rootfsB( |$)' "${cmdline_file}"; then
        printf '%s\n' "B"
        return 0
    fi
    uuid_a="$(board_read_partuuid_env A)"
    uuid_b="$(board_read_partuuid_env B)"
    if [ -n "${uuid_a}" ] && grep -Eq "(^| )root=PARTUUID=${uuid_a}( |$)" "${cmdline_file}"; then
        printf '%s\n' "A"
        return 0
    fi
    if [ -n "${uuid_b}" ] && grep -Eq "(^| )root=PARTUUID=${uuid_b}( |$)" "${cmdline_file}"; then
        printf '%s\n' "B"
        return 0
    fi
    if grep -Eq '(^| )root=/dev/nfs( |$)' "${cmdline_file}"; then
        printf '%s\n' "NFS"
        return 0
    fi
    printf '%s\n' "UNKNOWN"
}

board_select_target_slot() {
    activeSlot="$(board_read_active_slot)"
    case "${activeSlot}" in
        A) printf '%s\n' "B" ;;
        B) printf '%s\n' "A" ;;
        *) printf '%s\n' "B" ;;
    esac
}

board_select_swu_mode() {
    targetSlot="$(board_select_target_slot)"
    case "${targetSlot}" in
        A) printf '%s\n' "stable,slotA" ;;
        B) printf '%s\n' "stable,slotB" ;;
        *)
            printf '未知目标槽位=%s，默认写入 slotB\n' "${targetSlot}" >&2
            printf '%s\n' "stable,slotB"
            ;;
    esac
}

board_require_safe_upgrade_state() {
    upgradeAvailable="$(board_read_upgrade_available)"
    activeSlot="$(board_read_active_slot)"
    currentSlot="$(board_read_current_slot)"

    if [ "${upgradeAvailable}" = "1" ]; then
        printf '检测到上次升级尚未完成提交，请先重启并确认新槽位启动成功后再继续升级\n' >&2
        return 1
    fi

    case "${currentSlot}" in
        A|B)
            if [ "${currentSlot}" != "${activeSlot}" ]; then
                printf '当前实际启动槽位=%s，但环境 active_slot=%s；这通常表示升级后尚未重启，已拒绝继续升级\n' \
                    "${currentSlot}" "${activeSlot}" >&2
                return 1
            fi
            ;;
        NFS)
            printf '当前系统运行在 NFS 根文件系统上，继续按 active_slot=%s 选择目标槽位\n' "${activeSlot}"
            ;;
        *)
            printf '无法从 %s 判断当前启动槽位，已拒绝继续升级\n' "${BOARD_CMDLINE_FILE}" >&2
            return 1
            ;;
    esac
    return 0
}
