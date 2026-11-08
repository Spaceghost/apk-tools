#!/bin/sh

TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

mkdir -p files/etc files/usr/bin
echo data > files/etc/file
echo bin > files/usr/bin/file

$APK mkpkg -I name:symtest -I version:1.0 -F files -o symtest-1.0.apk

# The confinement root of the fsdir resolution is the extraction
# destination, so the escape target lives outside of it.

mkdir dest escape

# Absolute symlink target: re-rooted at the destination, fails closed
# as the target does not exist there
ln -s "$PWD"/escape dest/etc
$APK extract symtest-1.0.apk --destination dest && assert "absolute symlink escape not detected"
[ -e escape/file ] && assert "file written outside of destination root"
[ -e dest/etc/file ] && assert "file written through absolute symlink"

# Relative symlink target escaping via ..: clamped at the destination
rm dest/etc
ln -s ../escape dest/etc
$APK extract symtest-1.0.apk --destination dest && assert "relative symlink escape not detected"
[ -e escape/file ] && assert "file written outside of destination root"

# Absolute symlink target that exists below the destination root:
# re-rooting resolves and the file lands in the target. The dir entry
# collides with the planted symlink and is reported as a warning, so
# only the file extraction is asserted.
mkdir -p pkg2/link
echo data > pkg2/link/file
$APK mkpkg -I name:symtest2 -I version:1.0 -F pkg2 -o symtest2-1.0.apk

mkdir -p dest2/real
ln -s /real dest2/link
$APK extract symtest2-1.0.apk --destination dest2 2>dest2.log || :
[ "$(cat dest2/real/file)" = data ] || assert "file not extracted via absolute symlink"
[ -L dest2/link ] || assert "absolute symlink replaced"
grep -q "link/file" dest2.log && assert "link/file extraction warned"

exit 0
