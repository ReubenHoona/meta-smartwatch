SUMMARY = "Aurora: keep the modem in low power mode"
DESCRIPTION = "\
Nothing in the Halium container puts the LTE modem into low power mode, which \
the Android telephony framework does on stock: after boot the modem keeps \
searching for networks, costing battery on a watch without a cellular stack. \
aurora-modem-lpm watches the modem's QMI Device Management Service and sets \
low power mode at boot, after modem restarts and when the mode changes. GNSS \
and the modem-hosted Wi-Fi firmware keep working in low power mode."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "aurora"
PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit systemd

SRC_URI = "file://aurora-modem-lpm.c \
           file://aurora-modem-lpm.service"
S = "${UNPACKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} ${S}/aurora-modem-lpm.c -o ${B}/aurora-modem-lpm
}

do_install() {
    install -d ${D}${libexecdir}
    install -m 0755 ${B}/aurora-modem-lpm ${D}${libexecdir}/aurora-modem-lpm
    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${UNPACKDIR}/aurora-modem-lpm.service ${D}${systemd_unitdir}/system/aurora-modem-lpm.service
}

SYSTEMD_SERVICE:${PN} = "aurora-modem-lpm.service"
FILES:${PN} = "${libexecdir}/aurora-modem-lpm \
               ${systemd_unitdir}/system/aurora-modem-lpm.service"
