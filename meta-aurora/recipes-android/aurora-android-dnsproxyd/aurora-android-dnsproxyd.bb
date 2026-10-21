SUMMARY = "Aurora: DNS for the Android container"
DESCRIPTION = "\
Bionic resolves host names through netd's dnsproxyd socket, but the Halium \
container only has a stub netd, so every lookup inside the container fails. \
That keeps Qualcomm's xtra-daemon from downloading GNSS assistance data. \
aurora-dnsproxyd answers the container's getaddrinfo/gethostbyname requests \
on /dev/socket/dnsproxyd with the host resolver."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "aurora"
PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit systemd

SRC_URI = "file://aurora-dnsproxyd.c \
           file://aurora-dnsproxyd-start \
           file://aurora-dnsproxyd.service"
S = "${UNPACKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} ${S}/aurora-dnsproxyd.c -o ${B}/aurora-dnsproxyd
}

do_install() {
    install -d ${D}${libexecdir}
    install -m 0755 ${B}/aurora-dnsproxyd ${D}${libexecdir}/aurora-dnsproxyd
    install -m 0755 ${UNPACKDIR}/aurora-dnsproxyd-start ${D}${libexecdir}/aurora-dnsproxyd-start
    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${UNPACKDIR}/aurora-dnsproxyd.service ${D}${systemd_unitdir}/system/aurora-dnsproxyd.service
}

SYSTEMD_SERVICE:${PN} = "aurora-dnsproxyd.service"
RDEPENDS:${PN} = "lxc"
FILES:${PN} = "${libexecdir}/aurora-dnsproxyd \
               ${libexecdir}/aurora-dnsproxyd-start \
               ${systemd_unitdir}/system/aurora-dnsproxyd.service"
