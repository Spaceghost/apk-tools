#!/bin/sh

TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

# '..' inside a symlink target is resolved like the kernel: it climbs
# physically from the directory the preceding components resolve to
mkdir -p p1/p/l
echo data > p1/p/l/file
$APK mkpkg -I name:d -I version:1.0 -F p1 -o d-1.0.apk

mkdir -p "$TEST_ROOT"/p/real1 "$TEST_ROOT"/p/real2
ln -s real1/../real2 "$TEST_ROOT"/p/l

$APK add --initdb $TEST_USERMODE d-1.0.apk
[ "$(cat "$TEST_ROOT"/p/real2/file)" = data ] || assert "dotdot-in-target resolution failed"
[ ! -e "$TEST_ROOT"/p/real1/file ] || assert "file extracted into wrong symlink target"
[ -L "$TEST_ROOT"/p/l ] || assert "l symlink replaced"

# A symlink target cannot climb above the destination root: '..' past
# the root clamps instead of escaping
mkdir -p a1/a/l
echo data > a1/a/l/file
$APK mkpkg -I name:c -I version:1.0 -F a1 -o c-1.0.apk

mkdir -p "$TEST_ROOT"/a "$TEST_ROOT"/top
ln -s ../../top "$TEST_ROOT"/a/l

$APK add c-1.0.apk
[ "$(cat "$TEST_ROOT"/top/file)" = data ] || assert "clamped climb did not land at destination root"
[ ! -e "$TEST_ROOT"/a/file ] || assert "file extracted into link directory"

# A symlink pointing at its own directory in the middle of a path:
# the rest of the path continues from the link's directory
mkdir -p s1/s/l/x
echo data > s1/s/l/x/file
$APK mkpkg -I name:s -I version:1.0 -F s1 -o s-1.0.apk

mkdir -p "$TEST_ROOT"/s
ln -s . "$TEST_ROOT"/s/l

$APK add s-1.0.apk
[ "$(cat "$TEST_ROOT"/s/x/file)" = data ] || assert "file not extracted through self-directory symlink"
[ -L "$TEST_ROOT"/s/l ] || assert "l symlink replaced"

# '.' is transparent: a '..' pops the last real component, not a
# preceding '.' - u/l -> "./../udest" resolves to <root>/udest
mkdir -p u1/u/l
echo data > u1/u/l/file
$APK mkpkg -I name:u -I version:1.0 -F u1 -o u-1.0.apk

mkdir -p "$TEST_ROOT"/u "$TEST_ROOT"/udest
ln -s "./../udest" "$TEST_ROOT"/u/l

$APK add u-1.0.apk
[ "$(cat "$TEST_ROOT"/udest/file)" = data ] || assert "dot-transparent climb did not land at destination root"
[ ! -e "$TEST_ROOT"/u/udest ] || assert "file extracted below the link directory"

# A '..' after an intermediate symlink climbs physically from the
# resolved directory, like the kernel: with link -> real/deep, the
# parent of link's resolution is real, not w
mkdir -p "$TEST_ROOT"/w/real/deep "$TEST_ROOT"/w/real/other "$TEST_ROOT"/w/other
ln -s real/deep "$TEST_ROOT"/w/link
ln -s link/../other "$TEST_ROOT"/w/x

mkdir -p v1/w/x
echo data > v1/w/x/file
$APK mkpkg -I name:v -I version:1.0 -F v1 -o v-1.0.apk

$APK add v-1.0.apk
[ "$(cat "$TEST_ROOT"/w/real/other/file)" = data ] || assert "file not placed at the physically resolved parent"
[ ! -e "$TEST_ROOT"/w/other/file ] || assert "file placed at the lexically popped location"

# A symlink loop must fail closed
mkdir -p l1/l
echo data > l1/l/file
$APK mkpkg -I name:e -I version:1.0 -F l1 -o e-1.0.apk

ln -s l "$TEST_ROOT"/l

if $APK add e-1.0.apk > add.log 2>&1; then
	assert "symlink loop not detected"
fi
grep -q "Too many levels\|ELOOP\|loop" add.log || assert "loop not reported: $(cat add.log)"
[ ! -e "$TEST_ROOT"/file ] || assert "file extracted through symlink loop"

exit 0
