#!/bin/sh

TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

# a-1.0 owns shared/
mkdir -p a1/shared
echo a > a1/shared/data
$APK mkpkg -I name:a -I version:1.0 -F a1 -o a-1.0.apk

# a-2.0 does not own shared/ anymore (upgrade purges it)
mkdir -p a2/other
echo a2 > a2/other/data
$APK mkpkg -I name:a -I version:2.0 -F a2 -o a-2.0.apk

# b-1.0 re-creates shared/ in the same transaction
mkdir -p b1/shared
echo b > b1/shared/data
$APK mkpkg -I name:b -I version:1.0 -F b1 -o b-1.0.apk

$APK add --initdb $TEST_USERMODE a-1.0.apk
[ "$(cat "$TEST_ROOT"/shared/data)" = a ] || assert "initial install failed"

# One transaction: upgrade a (purges shared/) and install b (re-creates
# it): the purged directory must not leave a stale cached fd behind
$APK add a-2.0.apk b-1.0.apk
[ "$(cat "$TEST_ROOT"/shared/data)" = b ] || assert "recreated shared/data wrong"
[ "$(cat "$TEST_ROOT"/other/data)" = a2 ] || assert "other/data missing"

exit 0
