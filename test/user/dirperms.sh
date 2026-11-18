#!/bin/sh

REQUIRE_FAKEROOT=yes
TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

mkdir -p files
mkdir -m7777 files/testdir

$APK mkpkg -I name:stickydir -I version:1.0 -F files -o stickydir-1.0.apk

$APK add --initdb $TEST_USERMODE stickydir-1.0.apk
[ "$(stat -c %a "$TEST_ROOT"/testdir)" = 7777 ] || assert "Wrong directory permissions"

# Upgrade applies changed directory permissions
mkdir -p pkg1/testdir2 pkg2/testdir2
chmod 700 pkg1/testdir2
chmod 755 pkg2/testdir2
$APK mkpkg -I name:chgdir -I version:1.0 -F pkg1 -o chgdir-1.0.apk
$APK mkpkg -I name:chgdir -I version:2.0 -F pkg2 -o chgdir-2.0.apk

$APK add chgdir-1.0.apk
[ "$(stat -c %a "$TEST_ROOT"/testdir2)" = 700 ] || assert "Wrong directory permissions"
$APK add chgdir-2.0.apk
[ "$(stat -c %a "$TEST_ROOT"/testdir2)" = 755 ] || assert "Directory permissions not updated on upgrade"

# Admin modified directory permissions are kept
chmod 711 "$TEST_ROOT"/testdir2
$APK add --force-refresh chgdir-1.0.apk
[ "$(stat -c %a "$TEST_ROOT"/testdir2)" = 711 ] || assert "Modified directory permissions overridden"
