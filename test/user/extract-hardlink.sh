#!/bin/sh

TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

if ! stat -c "%D:%i" /dev/null > /dev/null 2>&1; then
	dev_inode() {
		stat -f "%Xd:%i" "$@"
	}
else
	dev_inode() {
		stat -c "%D:%i" "$@"
	}
fi

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

mkdir -p files/a files/b
echo hello > files/a/zzz
ln files/a/zzz files/a/aaa
ln files/a/aaa files/a/bbb
ln files/a/bbb files/b/link

$APK mkpkg --compression=none -I name:extract-hardlink -I version:1.0 -F files -o extract-hardlink-1.0.apk

# hardlinks must be extracted referencing the target's inode: plain,
# multiple and cross-directory links
mkdir dest
$APK extract extract-hardlink-1.0.apk --destination dest || assert "extract failed"

[ "$(cat dest/a/aaa)" = hello ] || assert "a/aaa content"
[ "$(cat dest/b/link)" = hello ] || assert "b/link content"
INODE="$(dev_inode dest/a/aaa)"
[ "$(dev_inode dest/a/bbb)" = "$INODE" ] || assert "a/bbb hardlink"
[ "$(dev_inode dest/a/zzz)" = "$INODE" ] || assert "a/zzz hardlink"
[ "$(dev_inode dest/b/link)" = "$INODE" ] || assert "b/link cross-directory hardlink"

# a hardlink target that is not an extracted regular file must be
# rejected while the rest of the package is still extracted: retarget
# all links at a same-length path ("a/xyz") not present in the package
replace_bytes extract-hardlink-1.0.apk ehlneg.apk 612f616161 612f78797a 1

mkdir dest2
$APK extract ehlneg.apk --destination dest2 2>ehlneg.log && assert "dangling hardlink target not rejected"
grep -q "hardlink target is not an extracted regular file" ehlneg.log || assert "no rejection warning"
[ "$(cat dest2/a/aaa)" = hello ] || assert "a/aaa not extracted despite valid payload"
[ ! -e dest2/a/bbb ] || assert "rejected hardlink bbb created"
[ ! -e dest2/a/zzz ] || assert "rejected hardlink zzz created"
[ ! -e dest2/b/link ] || assert "rejected hardlink link created"

exit 0
