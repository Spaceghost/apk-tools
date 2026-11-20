#!/bin/sh

TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

# Package 1.0 ships both /bin/tool (symlink) and /usr/bin/tool (regular file).
# Package 2.0 ships only /usr/bin/tool.
# On a usr-merged system /bin is a symlink to usr/bin, so the paths of the
# removed /bin/tool resolve to the very file that 2.0 just installed.
create_pkg() {
	local name="$1" ver="$2" tool="$3" with_bin_symlink="$4"
	local pkgdir="files/$name-$ver"

	mkdir -p "$pkgdir"/usr/bin
	echo "package $ver" > "$pkgdir"/usr/bin/"$tool"

	if [ "$with_bin_symlink" = "yes" ]; then
		mkdir -p "$pkgdir"/bin
		ln -s ../usr/bin/"$tool" "$pkgdir"/bin/"$tool"
	fi

	$APK mkpkg -I "name:$name" -I "version:$ver" -F "$pkgdir" -o "$name-$ver.apk"
}

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

create_pkg test-a 1.0 tool yes
create_pkg test-a 2.0 tool no

$APK add --initdb $TEST_USERMODE test-a-1.0.apk
cd "$TEST_ROOT"
[ -f usr/bin/tool ] || assert "usr/bin/tool not found"
[ -L bin/tool ] || assert "bin/tool symlink not found"
cd - > /dev/null

# manual usr-merge: /bin becomes a symlink to usr/bin
rm "$TEST_ROOT"/bin/tool
rmdir "$TEST_ROOT"/bin
ln -s usr/bin "$TEST_ROOT"/bin

$APK add -vv test-a-2.0.apk
cd "$TEST_ROOT"
[ -f usr/bin/tool ] || assert "usr/bin/tool was removed by the upgrade"
[ "$(cat usr/bin/tool)" = "package 2.0" ] || assert "usr/bin/tool has unexpected content"
[ -e bin/tool ] || assert "bin/tool (via /bin symlink) not found"
[ -L bin ] || assert "/bin is no longer a symlink"
cd - > /dev/null

# Aliased duplicates in one package: both resolve into usr/bin and
# must fail with a directory aliasing error
create_pkg test-b 1.0 btool yes
create_pkg test-b 2.0 btool yes

$APK add test-b-1.0.apk 2>b.log && assert "install of aliased duplicates did not fail"
grep -q "files resolve to the same path" b.log || assert "alias conflict not reported for install"

$APK add test-b-2.0.apk 2>b2.log && assert "upgrade of aliased duplicates did not fail"
grep -q "files resolve to the same path" b2.log || assert "alias conflict not reported for upgrade"

# Aliased hardlink duplicates: caught at extraction time
mkdir -p files/test-c-1.0/usr/bin files/test-c-1.0/bin
echo "package 1.0" > files/test-c-1.0/usr/bin/ctool
ln files/test-c-1.0/usr/bin/ctool files/test-c-1.0/bin/ctool
$APK mkpkg -I name:test-c -I version:1.0 -F files/test-c-1.0 -o test-c-1.0.apk

$APK add test-c-1.0.apk 2>c.log && assert "install of aliased hardlinks did not fail"
grep -q "files resolve to the same path" c.log || assert "alias conflict not reported for hardlink"

exit 0
