FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# 启用 BusyBox ntpd，供开机一次性校时
SRC_URI += "file://ntpd.cfg"
