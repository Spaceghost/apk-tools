#!/bin/sh

# Checks that setuid/setgid is cleared from the inode of a file that gets replaced or removed.

TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

create_pkg() {
	local name="$1" ver="$2" mode="$3"
	local pkgdir="files/${name}-${ver}"

	mkdir -p "$pkgdir"/bin
	echo "prog v${ver}" > "$pkgdir"/bin/prog
	chmod "$mode" "$pkgdir"/bin/prog

	$APK mkpkg -I name:"$name" -I "version:${ver}" -F "$pkgdir" -o "${name}-${ver}.apk"
}

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

fixup_setsuid() {
	# usermode does not set suid/sgid bits, set them manually
	chmod 4755 "$TEST_ROOT"/bin/prog
}

create_pkg test-a 1.0 4755
create_pkg test-a 2.0 4755
create_pkg test-b 1.0 0755

$APK add --initdb $TEST_USERMODE test-a-1.0.apk
fixup_setsuid

# Replaced file with setuid in the installed db
cd "$TEST_ROOT"
[ -u bin/prog ] || assert "setuid not set on installed file"
ln bin/prog prog.old
cd - > /dev/null

$APK add test-a-2.0.apk

cd "$TEST_ROOT"
[ "$(cat bin/prog)" = "prog v2.0" ] || assert "file not upgraded"
[ ! bin/prog -ef prog.old ] || assert "file was not replaced"
if [ "$(id -u)" = 0 ]; then
	[ -u bin/prog ] || assert "setuid cleared from the new file"
fi
[ -u prog.old ] && assert "setuid not cleared from replaced file"
rm prog.old
ln bin/prog prog.old
fixup_setsuid
cd - > /dev/null

# Removed file with setuid in the installed db
$APK del test-a

cd "$TEST_ROOT"
[ -e bin/prog ] && assert "file not removed"
[ -u prog.old ] && assert "setuid not cleared from removed file"
rm prog.old
cd - > /dev/null

# File without setuid in the installed db is not touched
$APK add $TEST_USERMODE test-b-1.0.apk
fixup_setsuid

cd "$TEST_ROOT"
ln bin/prog prog.old
cd - > /dev/null

$APK del test-b

cd "$TEST_ROOT"
[ -e bin/prog ] && assert "file not removed"
[ -u prog.old ] || assert "setuid cleared without db record of it"
cd - > /dev/null

exit 0
