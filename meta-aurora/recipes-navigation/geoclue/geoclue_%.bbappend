RDEPENDS:${PN}:append:aurora = " geoclue-provider-hybris-binder "

# Only the GPS provider is useful on the watch. The other providers need a
# cell modem, a street address, manual setup or web services that are gone.
do_install:append:aurora() {
    for p in example geonames gsmloc hostip localnet manual nominatim plazes yahoo; do
        rm -f ${D}${libexecdir}/geoclue-$p ${D}${datadir}/geoclue-providers/geoclue-$p.provider
    done
    rm -f ${D}${datadir}/dbus-1/services/org.freedesktop.Geoclue.Providers.*.service
}
