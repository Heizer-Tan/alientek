DESCRIPTION = "U-Boot for Alientek i.MX6ULL Alpha (mainline v2025.04)"

PV = "2025.04"
SRC_URI = "https://ftp.denx.de/pub/u-boot/u-boot-${PV}.tar.bz2"
SRC_URI[sha256sum] = "439d3bef296effd54130be6a731c5b118be7fddd7fcc663ccbc5fb18294d8718"

S = "${WORKDIR}/u-boot-${PV}"
