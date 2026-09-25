DESCRIPTION = "U-Boot for Alientek i.MX6ULL Alpha (mainline v2026.07)"

PV = "2026.07"
SRC_URI = "https://ftp.denx.de/pub/u-boot/u-boot-${PV}.tar.bz2"
SRC_URI[sha256sum] = "78e8bfc382fe388f9b55aa1daf8c563522a037779b5d4c349d1415e381f1243e"

S = "${WORKDIR}/u-boot-${PV}"
