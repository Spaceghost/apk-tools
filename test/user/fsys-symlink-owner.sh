#!/bin/sh

REQUIRE_FAKEROOT=yes
TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

require_programs chown

setup_apkroot

# Stay out of the unshare/chroot: the namespace switch breaks the
# fakeroot ownership emulation, and the checks under test are
# dirfd-based either way. Overwrites are forced as the packages reuse
# the same file paths; the behaviour under test is the symlink
# ownership policy, not the file conflict policy.
APK="$APK --allow-untrusted --no-interactive --force-no-chroot --force-overwrite"

# The package directories are built with the ownership they will have on
# disk, so that the only ownership mismatch left is the planted symlink
mkdir -p files1/tmp/payload
echo data > files1/tmp/payload/file
$APK mkpkg -I name:o1 -I version:1.0 -F files1 -o o1-1.0.apk

mkdir -p files2/tmp/own/payload
chown 1000 files2/tmp/own
echo data > files2/tmp/own/payload/file
$APK mkpkg -I name:o2 -I version:1.0 -F files2 -o o2-1.0.apk

mkdir -p files3/tmp/payload
echo data > files3/tmp/payload/file
$APK mkpkg -I name:o3 -I version:1.0 -F files3 -o o3-1.0.apk
$APK mkpkg -I name:o4 -I version:1.0 -F files3 -o o4-1.0.apk

# A symlink created by a non-root user in a world-writable directory
# must not redirect extraction into a directory the user does not own
chmod 777 "$TEST_ROOT"/tmp
ln -s ../../etc "$TEST_ROOT"/tmp/payload
chown -h 1000 "$TEST_ROOT"/tmp/payload

$APK add --initdb o1-1.0.apk && assert "unowned symlink traversal not detected"
[ ! -e "$TEST_ROOT"/etc/file ] || assert "file written through unowned symlink"
rm "$TEST_ROOT"/tmp/payload

# Same when the containing directory is owned by the symlink's owner,
# but the target directory is not
mkdir -p "$TEST_ROOT"/tmp/own
chown 1000 "$TEST_ROOT"/tmp/own
ln -s ../../etc "$TEST_ROOT"/tmp/own/payload
chown -h 1000 "$TEST_ROOT"/tmp/own/payload

$APK add o2-1.0.apk && assert "unowned target symlink traversal not detected"
[ ! -e "$TEST_ROOT"/etc/file ] || assert "file written to unowned target"
rm "$TEST_ROOT"/tmp/own/payload

# Drop the broken o1/o2 from world and db: the solver would otherwise
# keep reinstalling (and erroring on) them in the following commits
$APK del o1 o2 || assert "failed to remove broken o1/o2"

# A symlink owned by the same user as the directory it resolves to only
# redirects within the user's own space: traversal is allowed
mkdir -p "$TEST_ROOT"/tmp/attacker
chown -R 1000 "$TEST_ROOT"/tmp/attacker
ln -s attacker "$TEST_ROOT"/tmp/payload
chown -h 1000 "$TEST_ROOT"/tmp/payload

$APK add o3-1.0.apk || assert "own-directory symlink rejected"
[ "$(cat "$TEST_ROOT"/tmp/attacker/file)" = data ] || assert "file not extracted through own-directory symlink"

# Root-owned symlinks are part of the managed tree: traversal is allowed
rm "$TEST_ROOT"/tmp/payload
mkdir -p "$TEST_ROOT"/tmp/real
ln -s real "$TEST_ROOT"/tmp/payload

$APK add o4-1.0.apk || assert "root-owned symlink rejected"
[ "$(cat "$TEST_ROOT"/tmp/real/file)" = data ] || assert "file not extracted behind root-owned symlink"

# An unowned symlink re-evaluated inside the '..' pop fallback: the seed
# key contains the symlink component, so popping past it misses the cache
# and the prefix is resolved again - with the link reowned
mkdir -p files5/d/real/seed
echo data > files5/d/real/seed/f
$APK mkpkg -I name:o5 -I version:1.0 -F files5 -o o5-1.0.apk

mkdir -p "$TEST_ROOT"/d/real
ln -s real "$TEST_ROOT"/d/l

$APK add o5-1.0.apk || assert "o5 install through root-owned symlink failed"
[ "$(cat "$TEST_ROOT"/d/real/seed/f)" = data ] || assert "o5 file not extracted through symlink"

rm "$TEST_ROOT"/d/l
ln -s real "$TEST_ROOT"/d/l
chown -h 1000 "$TEST_ROOT"/d/l

mkdir -p files6/d/l/seed/s
echo data > files6/d/l/seed/s/g
$APK mkpkg -I name:o6 -I version:1.0 -F files6 -o o6-1.0.apk

ln -s .. "$TEST_ROOT"/d/l/seed/s

$APK add o6-1.0.apk 2>o6.log && assert "unowned symlink not detected via pop fallback"
grep -q "Operation not permitted" o6.log || assert "pop fallback failure was not a permission error"
[ "$(stat -c %u "$TEST_ROOT"/d/l)" = 1000 ] || assert "planted symlink ownership changed"
[ ! -e "$TEST_ROOT"/d/real/g ] || assert "file written through unowned symlink"

exit 0
