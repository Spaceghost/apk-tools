#!/bin/sh

TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

# Cache entries resolved through a symlink are invalidated when apk replaces
# or removes the symlink, or removes a directory, so that extraction done
# after the change does not follow the stale resolution.

# bbb ships only the d/l symlink; the target directories are unmanaged
mkdir -p b1/d
ln -s real1 b1/d/l
$APK mkpkg -I name:bbb -I version:1.0 -F b1 -o bbb-1.0.apk

mkdir -p b2/d
ln -s real2 b2/d/l
$APK mkpkg -I name:bbb -I version:2.0 -F b2 -o bbb-2.0.apk

# aaa and ccc ship files below the symlink path
mkdir -p a1/d/l
echo one > a1/d/l/x
$APK mkpkg -I name:aaa -I version:1.0 -F a1 -o aaa-1.0.apk

mkdir -p c1/d/l
echo two > c1/d/l/y
$APK mkpkg -I name:ccc -I version:1.0 -F c1 -o ccc-1.0.apk

mkdir "$TEST_ROOT"/d "$TEST_ROOT"/d/real1 "$TEST_ROOT"/d/real2

$APK add --initdb $TEST_USERMODE bbb-1.0.apk
[ "$(readlink "$TEST_ROOT"/d/l)" = real1 ] || assert "initial symlink missing"

# One transaction: aaa extracts through the old symlink target, bbb then
# retargets the symlink, ccc must resolve through the new target
$APK add aaa-1.0.apk bbb-2.0.apk ccc-1.0.apk

[ "$(readlink "$TEST_ROOT"/d/l)" = real2 ] || assert "symlink not retargeted"
[ "$(cat "$TEST_ROOT"/d/real1/x)" = one ] || assert "x not committed via old target"
[ "$(cat "$TEST_ROOT"/d/real2/y)" = two ] || assert "y not committed via retargeted symlink target"
[ "$(cat "$TEST_ROOT"/d/l/y)" = two ] || assert "y not visible via new target"

# dd-1.0 owns the q/l symlink and the real directory tree it points to
mkdir -p dd1/q/t/y
ln -s t dd1/q/l
echo z > dd1/q/t/y/z
$APK mkpkg -I name:dd -I version:1.0 -F dd1 -o dd-1.0.apk

# dd-2.0 owns only a file below q/l; the symlink and q/t are purged
mkdir -p dd2/q/l
echo w > dd2/q/l/w
$APK mkpkg -I name:dd -I version:2.0 -F dd2 -o dd-2.0.apk

# ee ships another file below q/l
mkdir -p ee1/q/l
echo v > ee1/q/l/v
$APK mkpkg -I name:ee -I version:1.0 -F ee1 -o ee-1.0.apk

$APK add dd-1.0.apk
[ "$(cat "$TEST_ROOT"/q/t/y/z)" = z ] || assert "initial dd install failed"

# dd upgrade purges the symlink and the q/t subtree; ee's file must then
# fail loudly like kernel path resolution would, not get written to the
# removed directory through a stale cached fd
if $APK add dd-2.0.apk ee-1.0.apk > add.log 2>&1; then
	assert "extraction succeeded through invalidated symlink"
fi
grep -q "failed to extract q/l/v" add.log || assert "no error for q/l/v: $(cat add.log)"
[ ! -e "$TEST_ROOT"/q/l ] || assert "q/l symlink not purged"
[ ! -e "$TEST_ROOT"/q/t/y ] || assert "q/t/y directory not purged"

# Same for apk extract: multiple packages in one invocation. gg extracts a
# file through a pre-existing symlink (its d/l dir entry fails with EEXIST
# but the parent still resolves through the link), hh retargets the symlink,
# ii must then resolve through the new target
mkdir -p gg1/d/l
echo one > gg1/d/l/x
$APK mkpkg -I name:gg -I version:1.0 -F gg1 -o gg-1.0.apk

mkdir -p hh1/d
ln -s real2 hh1/d/l
$APK mkpkg -I name:hh -I version:1.0 -F hh1 -o hh-1.0.apk

mkdir -p ii1/d/l
echo two > ii1/d/l/y
$APK mkpkg -I name:ii -I version:1.0 -F ii1 -o ii-1.0.apk

mkdir -p dest/d/real1 dest/d/real2
ln -s real1 dest/d/l

# gg's and ii's d/l dir entries fail with EEXIST (symlink in place) and are
# reported as warnings, making extract exit non-zero
if ! $APK extract gg-1.0.apk hh-1.0.apk --force-overwrite ii-1.0.apk --destination dest; then :; fi

[ "$(cat "$TEST_ROOT"/tmp/dest/d/real1/x)" = one ] || assert "x not extracted via old target"
[ "$(readlink "$TEST_ROOT"/tmp/dest/d/l)" = real2 ] || assert "symlink not retargeted"
[ "$(cat "$TEST_ROOT"/tmp/dest/d/real2/y)" = two ] || assert "y not extracted via retargeted symlink target"

exit 0
