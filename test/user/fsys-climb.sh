#!/bin/sh

TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

# a caches the "etc" prefix directory
mkdir -p a1/etc
echo keep > a1/etc/keep
$APK mkpkg -I name:a -I version:1.0 -F a1 -o a-1.0.apk

# b extracts a file behind a pre-planted symlink that climbs above the
# cached seed directory: the resolution has to fall back to resolving
# the full path from the destination root
mkdir -p b1/etc/conf
echo other > b1/etc/other
echo data > b1/etc/conf/data
$APK mkpkg -I name:b -I version:1.0 -F b1 -o b-1.0.apk

$APK add --initdb $TEST_USERMODE a-1.0.apk

mkdir "$TEST_ROOT"/real
ln -s ../real "$TEST_ROOT"/etc/conf

$APK add b-1.0.apk

[ "$(cat "$TEST_ROOT"/real/data)" = data ] || assert "data not in symlink target"
[ "$(cat "$TEST_ROOT"/etc/other)" = other ] || assert "etc/other missing"
[ -L "$TEST_ROOT"/etc/conf ] || assert "etc/conf symlink replaced"
[ ! -e "$TEST_ROOT"/etc/realconf ] || assert "clamped resolution created etc/realconf"

# A '..' popping into an uncached prefix: the seed key contains the
# symlink component, so the pop misses the cache and the prefix is
# re-resolved by a nested walk
mkdir -p u2/deep/x/seed/s
echo data > u2/deep/x/seed/f
echo data > u2/deep/x/seed/s/g
$APK mkpkg -I name:deep -I version:1.0 -F u2 -o deep-1.0.apk

mkdir -p "$TEST_ROOT"/deep/y/seed
ln -s y "$TEST_ROOT"/deep/x
ln -s .. "$TEST_ROOT"/deep/y/seed/s

$APK add deep-1.0.apk
[ "$(cat "$TEST_ROOT"/deep/y/seed/f)" = data ] || assert "seed file not extracted through symlink"
[ "$(cat "$TEST_ROOT"/deep/y/g)" = data ] || assert "pop fallback did not re-resolve the prefix"
[ -L "$TEST_ROOT"/deep/x ] || assert "x symlink replaced"

exit 0
