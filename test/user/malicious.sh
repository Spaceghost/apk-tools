#!/bin/sh

TESTDIR=$(realpath "${TESTDIR:-"$(dirname "$0")"/..}")
. "$TESTDIR"/testlib.sh

setup_apkroot
APK="$APK --allow-untrusted --no-interactive"

mkdir -p files/a files/dir===dir
echo hello > files/a/file=file

$APK mkpkg --compression=none -I name:malicious -I version:1.0 -F files -o malicious-1.0.apk

echo hello > files/a/"file	file"
$APK mkpkg --compression=none -I name:malicious -I version:1.0 -F files -o malicious-1.0b.apk && assert "mkpkg filename check failed"

mkdir dest dest-a dest-b
$APK extract malicious-1.0.apk --destination dest || assert "failed to extract"

replace_bytes malicious-1.0.apk malicious-1.0-a.apk 6469723d3d3d646972 6469722f2e2f646972 1
$APK extract malicious-1.0-a.apk --destination dest-a && assert "malicious check 1 failed"

replace_bytes malicious-1.0.apk malicious-1.0-b.apk 66696c653d66696c65 66696c652f66696c65 1
$APK extract malicious-1.0-b.apk --destination dest-b && assert "malicious check 2 failed"

exit 0
