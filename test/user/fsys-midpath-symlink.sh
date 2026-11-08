#!/bin/sh

TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

mkdir -p b1/a/b/c
echo keep > b1/a/keep
echo tool > b1/a/b/c/tool
$APK mkpkg -I name:m -I version:1.0 -F b1 -o m-1.0.apk

# The symlink is in the middle of the resolution path: "a" is cached as
# the seed directory, and the remainder "b/c" walks through the symlink
mkdir "$TEST_ROOT"/a "$TEST_ROOT"/real
ln -s ../real "$TEST_ROOT"/a/b

$APK add --initdb $TEST_USERMODE m-1.0.apk

[ "$(cat "$TEST_ROOT"/real/c/tool)" = tool ] || assert "tool not in symlink target"
[ "$(cat "$TEST_ROOT"/a/keep)" = keep ] || assert "a/keep missing"
[ -L "$TEST_ROOT"/a/b ] || assert "a/b symlink replaced"

# Multiple symlinks in one path: each splice continues from the
# previous resolution
mkdir -p m2/chain/l1/l2/l3
echo data > m2/chain/l1/l2/l3/file
$APK mkpkg -I name:mid2 -I version:1.0 -F m2 -o mid2-1.0.apk

mkdir -p "$TEST_ROOT"/chain/b/c/real
ln -s b "$TEST_ROOT"/chain/l1
ln -s c "$TEST_ROOT"/chain/b/l2
ln -s real "$TEST_ROOT"/chain/b/c/l3

$APK add mid2-1.0.apk
[ "$(cat "$TEST_ROOT"/chain/b/c/real/file)" = data ] || assert "file not extracted through the symlink chain"
[ -L "$TEST_ROOT"/chain/l1 ] || assert "l1 symlink replaced"
[ -L "$TEST_ROOT"/chain/b/l2 ] || assert "l2 symlink replaced"
[ -L "$TEST_ROOT"/chain/b/c/l3 ] || assert "l3 symlink replaced"

exit 0
