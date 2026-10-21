SUMMARY = "Aurora: IStats stand-in for the Android container"
DESCRIPTION = "\
mcu_mgmtd, which manages the watch's low-power co-processor, waits for \
android.frameworks.stats.IStats/default before finishing its start-up. The \
container has no Android framework to provide it, so mcu_mgmtd never brought \
up its MCU watchdog and event connection, and polled the service manager every \
second, keeping the CPU awake. aurora-istats registers the service and drops \
what is reported to it."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "aurora"
PACKAGE_ARCH = "${MACHINE_ARCH}"

DEPENDS = "libgbinder glib-2.0"

inherit pkgconfig systemd

SRC_URI = "file://aurora-istats.c \
           file://aurora-istats.service"
S = "${UNPACKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} ${S}/aurora-istats.c -o ${B}/aurora-istats \
        $(pkg-config --cflags --libs libgbinder glib-2.0)
}

do_install() {
    install -d ${D}${libexecdir}
    install -m 0755 ${B}/aurora-istats ${D}${libexecdir}/aurora-istats
    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${UNPACKDIR}/aurora-istats.service ${D}${systemd_unitdir}/system/aurora-istats.service
}

SYSTEMD_SERVICE:${PN} = "aurora-istats.service"
FILES:${PN} = "${libexecdir}/aurora-istats \
               ${systemd_unitdir}/system/aurora-istats.service"
