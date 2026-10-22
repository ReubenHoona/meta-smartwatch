# meta-asteroid-community pins asteroid-map to its last Qt5 revision; the
# image is Qt6, so build the Qt6 port. Positions come from geoclue-master
# through the geoclue0 QtPositioning plugin.
SRCREV:aurora = "8263b2175843e83c32baee2eec5380559db2b735"
RDEPENDS:${PN}:append:aurora = " qtpositioning-qmlplugins qtpositioning-geoclue geoclue"
