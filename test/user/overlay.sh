#!/bin/sh

REQUIRE_FAKEROOT=yes
TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

setup_apkroot
APK="$APK --allow-untrusted --no-interactive --force-no-chroot"

mkdir -p pkg/etc pkg/data pkg/root pkg/baselayout/root "$TEST_ROOT"/etc
# Overlay explicitly lists data/, so it exists on disk with the mode
# from the tar, and packages must not override it
mkdir -m 700 "$TEST_ROOT"/data
for f in etc/a etc/b etc/c data/d data/e; do
	echo "package" > pkg/"$f"
	echo "overlay" > "$TEST_ROOT"/"$f"
done

# First level directory missing from the overlay: extraction created
# the parent with default permissions
mkdir -m 755 "$TEST_ROOT"/root
mkdir -m 700 "$TEST_ROOT"/root/.ssh
echo "overlay" > "$TEST_ROOT"/root/.ssh/authorized_keys

# alpine-baselayout style: omitted directory with strict permissions
chmod 700 pkg/baselayout/root
$APK mkpkg -F pkg -I name:overlay -I version:1.0 -o overlay-1.0.apk
$APK mkpkg -F pkg/baselayout -I name:baselayout -I version:1.0 -o baselayout-1.0.apk

$APK add --initdb $TEST_USERMODE --overlay-from-stdin overlay-1.0.apk baselayout-1.0.apk > apk-stdout.log 2>&1 <<EOF || assert "install fail"
etc/b
data/
data/e
root/.ssh/
root/.ssh/authorized_keys
EOF

cat apk-stdout.log >&2

[ "$(stat -c %a "$TEST_ROOT"/root)" = 700 ] || assert "root/ permissions not corrected, got $(stat -c %a "$TEST_ROOT"/root)"
[ "$(stat -c %a "$TEST_ROOT"/root/.ssh)" = 700 ] || assert "root/.ssh permissions changed"
[ "$(stat -c %a "$TEST_ROOT"/data)" = 700 ] || assert "overlay listed data/ permissions overridden, got $(stat -c %a "$TEST_ROOT"/data)"
[ "$(cat "$TEST_ROOT"/root/.ssh/authorized_keys)" = "overlay" ] || assert "overlay file lost"

diff -u - apk-stdout.log <<EOF || assert "wrong scripts result"
(1/2) Installing baselayout (1.0)
(2/2) Installing overlay (1.0)
  Installing file to etc/a.apk-new
  Installing file to etc/c.apk-new
OK: 41 B in 2 packages
EOF

cd "$TEST_ROOT"
[ "$(cat etc/a)" = "overlay" ] || assert "etc/a updated unexpectedly"
[ "$(cat etc/a.apk-new)" = "package" ] || assert "etc/a.apk-new missing"
[ "$(cat etc/b)" = "overlay" ] || assert "etc/b updated unexpectedly"
[ ! -e "etc/b.apk-new" ] || assert "etc/b.apk-new exists"
[ "$(cat etc/c)" = "overlay" ] || assert "etc/c updated unexpectedly"
[ "$(cat etc/c.apk-new)" = "package" ] || assert "etc/c.apk-new missing"
[ "$(cat data/d)" = "package" ] || assert "data/d updated unexpectedly"
[ "$(cat data/e)" = "overlay" ] || assert "data/e updated unexpectedly"
