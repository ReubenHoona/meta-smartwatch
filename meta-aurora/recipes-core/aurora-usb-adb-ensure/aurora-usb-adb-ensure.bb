SUMMARY = "Aurora: keep usb-moded in adb_mode after boot"
DESCRIPTION = "\
At boot usb-moded can select its mode before adbd has its FunctionFS \
endpoints up; set_udc then fails and usb-moded falls back to charging-only \
(no adb) until the cable is re-plugged. This service watches the mode for a \
couple of minutes after boot and requests adb_mode again when needed."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "aurora"
PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit systemd

SRC_URI = "file://aurora-usb-adb-ensure \
           file://aurora-usb-adb-ensure.service"
S = "${UNPACKDIR}"

do_install() {
    install -d ${D}${libexecdir}
    install -m 0755 ${UNPACKDIR}/aurora-usb-adb-ensure ${D}${libexecdir}/aurora-usb-adb-ensure
    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${UNPACKDIR}/aurora-usb-adb-ensure.service ${D}${systemd_unitdir}/system/aurora-usb-adb-ensure.service
}

SYSTEMD_SERVICE:${PN} = "aurora-usb-adb-ensure.service"
FILES:${PN} = "${libexecdir}/aurora-usb-adb-ensure \
               ${systemd_unitdir}/system/aurora-usb-adb-ensure.service"
