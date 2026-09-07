FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://swupdate-minimal.cfg"

RDEPENDS:${PN} += "libgcc"
