#!/bin/sh

TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

# A symlink pointing to its own directory: resolving through it walks
# into a nested walk with an empty effective target, which must return
# an owned fd of the same directory
mkdir -p b1/link
echo data > b1/link/file
$APK mkpkg -I name:dot -I version:1.0 -F b1 -o dot-1.0.apk

ln -s . "$TEST_ROOT"/link

$APK add --initdb $TEST_USERMODE dot-1.0.apk

[ "$(cat "$TEST_ROOT"/link/file)" = data ] || assert "file not extracted through self-referencing symlink"
[ -L "$TEST_ROOT"/link ] || assert "link symlink replaced"

exit 0
