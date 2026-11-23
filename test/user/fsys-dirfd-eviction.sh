#!/bin/sh

TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

if ! stat -c "%D:%i" /dev/null > /dev/null 2>&1; then
	dev_inode() {
		stat -f "%Xd:%i" "$@"
	}
else
	dev_inode() {
		stat -c "%D:%i" "$@"
	}
fi

# The dirfd cache owns its fds and evicts deepest-first, so resolving a
# cross-directory hardlink's target directory can evict and close the fd
# of the directory being extracted into: linkat then fails with EBADF,
# or a reused fd number silently creates the hardlink in the target's
# directory.  Both variants are covered, via apk add and apk extract,
# with shallow and multi-component target directories.

# mkpkg_layout TARGET LINKDIR PKGNAME FILLERPREFIX
# A regular file at TARGET, a hardlink to it at LINKDIR/link, and 40
# one-file filler dirs.  TARGET and FILLERPREFIX must sort before
# LINKDIR/link so the target carries the data and the cache is full.
mkpkg_layout() {
	i=0
	while [ "$i" -lt 40 ]; do
		f=$(printf "%s%02d" "$4" "$i")
		mkdir -p "files/$f"
		echo x > "files/$f/x"
		i=$((i + 1))
	done
	mkdir -p "files/$(dirname "$1")" "files/$2"
	echo hello > "files/$1"
	ln "files/$1" "files/$2/link"
	$APK mkpkg --compression=none -I "name:$3" -I version:1.0 -F files -o "$3-1.0.apk"
}

rm -rf files
mkpkg_layout a/target c/d/e/f aaa ba

rm -rf files
mkpkg_layout p/q/r/s/t2 w1/w2/w3/w4 bbb ca

# apk add: a misplaced hardlink surfaces as a commit failure
$APK add --initdb $TEST_USERMODE aaa-1.0.apk > add-aaa.log 2>&1 ||
	assert "apk add aaa failed: $(cat add-aaa.log)"
[ "$(cat "$TEST_ROOT"/c/d/e/f/link)" = hello ] || assert "aaa link content"
INODE="$(dev_inode "$TEST_ROOT"/a/target)"
[ "$(dev_inode "$TEST_ROOT"/c/d/e/f/link)" = "$INODE" ] || assert "aaa link not a hardlink to a/target"
[ "$(ls -A "$TEST_ROOT"/a)" = target ] || assert "stray files next to a/target: $(ls -A "$TEST_ROOT"/a)"
[ "$(ls -A "$TEST_ROOT"/c/d/e/f)" = link ] || assert "stray files in c/d/e/f"

$APK add $TEST_USERMODE bbb-1.0.apk > add-bbb.log 2>&1 ||
	assert "apk add bbb failed: $(cat add-bbb.log)"
[ "$(cat "$TEST_ROOT"/w1/w2/w3/w4/link)" = hello ] || assert "bbb link content"
INODE="$(dev_inode "$TEST_ROOT"/p/q/r/s/t2)"
[ "$(dev_inode "$TEST_ROOT"/w1/w2/w3/w4/link)" = "$INODE" ] || assert "bbb link not a hardlink to p/q/r/s/t2"
[ "$(ls -A "$TEST_ROOT"/p/q/r/s)" = t2 ] || assert "stray files next to p/q/r/s/t2: $(ls -A "$TEST_ROOT"/p/q/r/s)"
[ "$(ls -A "$TEST_ROOT"/w1/w2/w3/w4)" = link ] || assert "stray files in w1/w2/w3/w4"

# apk extract has no commit phase: a misplaced hardlink is silent, so
# check for stray links explicitly
mkdir dest1
$APK extract aaa-1.0.apk --destination dest1 > extract-aaa.log 2>&1 ||
	assert "apk extract aaa failed: $(cat extract-aaa.log)"
[ "$(cat dest1/c/d/e/f/link)" = hello ] || assert "aaa extracted link content"
[ "$(dev_inode dest1/c/d/e/f/link)" = "$(dev_inode dest1/a/target)" ] || assert "aaa extracted link not a hardlink"
[ "$(find dest1 -name link)" = "dest1/c/d/e/f/link" ] || assert "stray hardlink in extract aaa: $(find dest1 -name link)"

mkdir dest2
$APK extract bbb-1.0.apk --destination dest2 > extract-bbb.log 2>&1 ||
	assert "apk extract bbb failed: $(cat extract-bbb.log)"
[ "$(cat dest2/w1/w2/w3/w4/link)" = hello ] || assert "bbb extracted link content"
[ "$(dev_inode dest2/w1/w2/w3/w4/link)" = "$(dev_inode dest2/p/q/r/s/t2)" ] || assert "bbb extracted link not a hardlink"
[ "$(find dest2 -name link)" = "dest2/w1/w2/w3/w4/link" ] || assert "stray hardlink in extract bbb: $(find dest2 -name link)"

exit 0
