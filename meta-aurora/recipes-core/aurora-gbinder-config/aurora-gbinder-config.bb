SUMMARY = "Aurora: libgbinder protocol settings for the Android 13 AIDL HALs"
DESCRIPTION = "\
/etc/gbinder.conf keeps ApiLevel 28 for the HIDL clients on hwbinder, which \
gives /dev/binder an AIDL wire protocol the Halium-13 container's service \
manager does not speak, so host gbinder clients cannot reach AIDL HALs such \
as android.hardware.gnss IGnss. This fragment sets the Android 13 protocol \
and service manager for /dev/binder only."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "aurora"
PACKAGE_ARCH = "${MACHINE_ARCH}"

SRC_URI = "file://aurora-aidl.conf"
S = "${UNPACKDIR}"

do_install() {
    install -d ${D}${sysconfdir}/gbinder.d
    install -m 0644 ${UNPACKDIR}/aurora-aidl.conf ${D}${sysconfdir}/gbinder.d/aurora-aidl.conf
}

FILES:${PN} = "${sysconfdir}/gbinder.d/aurora-aidl.conf"
