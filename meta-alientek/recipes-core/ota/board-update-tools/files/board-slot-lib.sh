#!/bin/sh
# 槽位辅助库：供 ota-apply 等脚本 source
# 识别 root= mmcblk*p{2,3}、PARTLABEL、PARTUUID（含 MBR 形 xxx-02/-03）；挂载源/blkid 兜底

BOARD_CMDLINE_FILE="${BOARD_CMDLINE_FILE:-/proc/cmdline}"

board_read_active_slot() {
	# fw_printenv 成功但值为空时也要回退默认 A（新烧录卡常见）
	val="$(fw_printenv -n active_slot 2>/dev/null || true)"
	case "${val}" in
	A|B) printf '%s\n' "${val}" ;;
	*) printf '%s\n' "A" ;;
	esac
}

board_read_ota_pending() {
	fw_printenv -n ota_pending 2>/dev/null || printf '%s\n' "0"
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

board_norm_uuid() {
	printf '%s' "$1" | tr 'A-F' 'a-f'
}

# / 的块设备，如 /dev/mmcblk0p2
board_root_block_dev() {
	if command -v findmnt >/dev/null 2>&1; then
		src="$(findmnt -n -o SOURCE / 2>/dev/null || true)"
		if [ -n "${src}" ]; then
			printf '%s\n' "${src}"
			return 0
		fi
	fi
	awk '$2 == "/" { print $1; exit }' /proc/mounts 2>/dev/null || true
}

# MBR PARTUUID 形如 076c4a2a-02 → 分区号 02=A、03=B
board_slot_from_mbr_partuuid() {
	pu="$(board_norm_uuid "$1")"
	case "${pu}" in
	*-02 | *-2)
		printf '%s\n' "A"
		return 0
		;;
	*-03 | *-3)
		printf '%s\n' "B"
		return 0
		;;
	esac
	return 1
}

board_slot_from_block_dev() {
	dev="$1"
	case "${dev}" in
	/dev/mmcblk*p2 | /dev/sd*[0-9]2)
		# sdX2 需更严，仅匹配末尾分区号 2/3 的 mmc
		;;
	esac
	case "${dev}" in
	/dev/mmcblk*p2)
		printf '%s\n' "A"
		return 0
		;;
	/dev/mmcblk*p3)
		printf '%s\n' "B"
		return 0
		;;
	/dev/nfs | *:/*)
		printf '%s\n' "NFS"
		return 0
		;;
	PARTUUID=*)
		pu="${dev#PARTUUID=}"
		if slot="$(board_slot_from_mbr_partuuid "${pu}")"; then
			printf '%s\n' "${slot}"
			return 0
		fi
		;;
	/dev/disk/by-partuuid/*)
		pu="${dev##*/}"
		if slot="$(board_slot_from_mbr_partuuid "${pu}")"; then
			printf '%s\n' "${slot}"
			return 0
		fi
		dev="PARTUUID=${pu}"
		;;
	esac

	# blkid 对照 mmcblk0/1 的 p2/p3
	if command -v blkid >/dev/null 2>&1; then
		want="$(board_norm_uuid "${dev#PARTUUID=}")"
		case "${dev}" in
		PARTUUID=*) ;;
		*) want="" ;;
		esac
		if [ -n "${want}" ]; then
			for pair in "2:A" "3:B"; do
				part="${pair%%:*}"
				slot="${pair##*:}"
				for disk in /dev/mmcblk0 /dev/mmcblk1; do
					[ -b "${disk}p${part}" ] || continue
					got="$(blkid -s PARTUUID -o value "${disk}p${part}" 2>/dev/null || true)"
					got="$(board_norm_uuid "${got}")"
					if [ -n "${got}" ] && [ "${got}" = "${want}" ]; then
						printf '%s\n' "${slot}"
						return 0
					fi
				done
			done
		fi
	fi
	return 1
}

board_read_current_slot() {
	cmdline_file="${BOARD_CMDLINE_FILE}"
	cmdline=""
	if [ -f "${cmdline_file}" ]; then
		cmdline="$(tr -d '\n' <"${cmdline_file}")"
	fi

	if printf '%s' "${cmdline}" | grep -Eq '(^| )root=/dev/mmcblk[0-9]+p2( |$)'; then
		printf '%s\n' "A"
		return 0
	fi
	if printf '%s' "${cmdline}" | grep -Eq '(^| )root=/dev/mmcblk[0-9]+p3( |$)'; then
		printf '%s\n' "B"
		return 0
	fi
	if printf '%s' "${cmdline}" | grep -Eq '(^| )root=PARTLABEL=rootfsA( |$)'; then
		printf '%s\n' "A"
		return 0
	fi
	if printf '%s' "${cmdline}" | grep -Eq '(^| )root=PARTLABEL=rootfsB( |$)'; then
		printf '%s\n' "B"
		return 0
	fi

	cmd_uuid="$(printf '%s' "${cmdline}" | sed -n 's/.*root=PARTUUID=\([^ ]*\).*/\1/ip')"
	cmd_uuid="$(board_norm_uuid "${cmd_uuid}")"
	if [ -n "${cmd_uuid}" ]; then
		# MBR：…-02 / …-03 直接对应 A/B（你板子 cmdline 即此形式）
		if slot="$(board_slot_from_mbr_partuuid "${cmd_uuid}")"; then
			printf '%s\n' "${slot}"
			return 0
		fi
		uuid_a="$(board_norm_uuid "$(board_read_partuuid_env A)")"
		uuid_b="$(board_norm_uuid "$(board_read_partuuid_env B)")"
		if [ -n "${uuid_a}" ] && [ "${cmd_uuid}" = "${uuid_a}" ]; then
			printf '%s\n' "A"
			return 0
		fi
		if [ -n "${uuid_b}" ] && [ "${cmd_uuid}" = "${uuid_b}" ]; then
			printf '%s\n' "B"
			return 0
		fi
		if slot="$(board_slot_from_block_dev "PARTUUID=${cmd_uuid}")"; then
			printf '%s\n' "${slot}"
			return 0
		fi
	fi

	if printf '%s' "${cmdline}" | grep -Eq '(^| )root=/dev/nfs( |$)'; then
		printf '%s\n' "NFS"
		return 0
	fi

	root_dev="$(board_root_block_dev)"
	if [ -n "${root_dev}" ] && slot="$(board_slot_from_block_dev "${root_dev}")"; then
		printf '%s\n' "${slot}"
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
	otaPending="$(board_read_ota_pending)"
	activeSlot="$(board_read_active_slot)"
	currentSlot="$(board_read_current_slot)"
	rawActive="$(fw_printenv -n active_slot 2>/dev/null || true)"

	# 环境从未写入 active_slot：按当前实际槽补齐，避免误拒升级
	if [ -z "${rawActive}" ]; then
		case "${currentSlot}" in
		A|B)
			fw_setenv active_slot "${currentSlot}" 2>/dev/null || true
			activeSlot="${currentSlot}"
			printf '已补写 active_slot=%s（原先为空）\n' "${activeSlot}" >&2
			;;
		esac
	fi

	if [ "${otaPending}" = "1" ]; then
		printf '检测到 ota_pending=1（上次升级尚未提交），请先重启并确认新槽位启动成功后再继续升级\n' >&2
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
		printf '无法判断当前启动槽位，已拒绝继续升级\n' >&2
		printf '  cmdline: %s\n' "$(tr -d '\n' <"${BOARD_CMDLINE_FILE}" 2>/dev/null || true)" >&2
		printf '  rootdev: %s\n' "$(board_root_block_dev)" >&2
		printf '  active_slot(env)=%s rootfs_a_partuuid=%s rootfs_b_partuuid=%s\n' \
			"${rawActive}" \
			"$(board_read_partuuid_env A)" \
			"$(board_read_partuuid_env B)" >&2
		return 1
		;;
	esac
	return 0
}
