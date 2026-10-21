SUMMARY = "Aurora: mark the running A/B slot as successfully booted"
DESCRIPTION = "\
abl keeps per-slot state in the GPT attributes of boot_a/boot_b. While a \
slot is not marked successful, abl decrements its retry count on every boot \
and finally marks it unbootable, after which the watch only enters fastboot. \
Normally the Halium container's boot HAL marks the slot; this timer is the \
fallback for boots where the container does not come up. It only flips the \
successful bit of the running slot in the primary GPT (as abl and the boot \
HAL do on this platform), refuses to write unless both GPT copies validate, \
and is a no-op when the slot is already marked."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "aurora"
PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit systemd

SRC_URI = "file://aurora-mark-boot-successful \
           file://aurora-mark-boot-successful.service \
           file://aurora-mark-boot-successful.timer"
S = "${UNPACKDIR}"

RDEPENDS:${PN} = "perl perl-module-strict perl-module-warnings"

do_install() {
    install -d ${D}${libexecdir}
    install -m 0755 ${UNPACKDIR}/aurora-mark-boot-successful ${D}${libexecdir}/aurora-mark-boot-successful
    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${UNPACKDIR}/aurora-mark-boot-successful.service ${D}${systemd_unitdir}/system/aurora-mark-boot-successful.service
    install -m 0644 ${UNPACKDIR}/aurora-mark-boot-successful.timer ${D}${systemd_unitdir}/system/aurora-mark-boot-successful.timer
}

SYSTEMD_SERVICE:${PN} = "aurora-mark-boot-successful.timer"
FILES:${PN} = "${libexecdir}/aurora-mark-boot-successful \
               ${systemd_unitdir}/system/aurora-mark-boot-successful.service \
               ${systemd_unitdir}/system/aurora-mark-boot-successful.timer"
