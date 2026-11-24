# Cascadia LAN package delivery

The fork can try a private BitTorrent swarm for an HTTP(S) package download.
The original repository still supplies the signed APK index, and apk checks
the fetched package against that index before using it. Torrent metadata uses
[BEP 19 webseeds](https://www.bittorrent.org/beps/bep_0019.html) so ordinary
HTTP servers with byte-range support can serve the same file. Missing
metadata, a failed transfer, or a rejected package falls back to the
original URL. Local packages, index downloads and offline mode use the
ordinary apk path.

Start with one immutable, commonly installed APK or a large abuild distfile.
Catalog creation and the optional helper require Python 3.11 or later and
aria2 1.37. Host an HTTP BitTorrent tracker on your LAN, for example
opentracker bound to a LAN interface. Keep the tracker and seeder ports
reachable from participating machines only. The helper disables DHT, peer
exchange and multicast discovery. Private torrent metadata identifies the
LAN tracker.

1. On the mirror or catalog publisher, generate metadata from the exact
   APK bytes you serve at the ordinary package URL. Optional repeated
   --webseed arguments add alternate HTTP URLs for identical bytes. Use the
   full immutable file URL for each webseed.

       install -Dm755 scripts/apk-webseed /usr/local/bin/apk-webseed
       export APK_WEBSEED_CATALOG=/srv/apk-webseed/catalog
       url=https://mirror.example/edge/main/x86_64/example-1.0-r0.apk
       apk-webseed publish --tracker http://tracker.lan:6969/announce \
         "$url" ./example-1.0-r0.apk

   The catalog records are keyed on the exact URL apk or abuild requests.
   Copy the generated JSON entries to each participating machine using your
   normal trusted configuration delivery. The standalone .torrent files are
   available for other BEP 19 clients. Keep catalog and helper executable
   writable only by the administrator of the corresponding apk or abuild
   account. Catalog files are generated with private permissions.

2. Start at least one uploader with the same local catalog. A seeder runs
   in the foreground and can be supervised by Theseus or OpenRC. Run it as
   a dedicated unprivileged user. Use a distinct port per seeder process
   on the same machine.

       export APK_WEBSEED_CACHE=/srv/apk-webseed/seed
       apk-webseed seed --port 6881 "$url" ./example-1.0-r0.apk

   Start additional seeders on machines with the same file to share upload
   capacity. One aria2 process can also host several .torrent files using
   its input-file format, with a distinct data directory containing a file
   named payload per torrent.

3. On a client, set the catalog path and a writable, private cache path for
   the account running apk. Invoke the forked apk with the helper path.

       export APK_WEBSEED_CATALOG=/etc/apk/webseed-catalog
       export APK_WEBSEED_CACHE=/var/cache/apk-webseed
       apk --webseed-fetcher /usr/local/bin/apk-webseed add example

   The helper keeps verified immutable payloads in this cache for later
   requests on the same machine. Clients exit after downloading. Seeder
   processes keep serving the swarm. Use a dedicated cache directory and
   prune old objects when neither clients nor seeders are using that cache.
   The supplied helper caps each swarm attempt at eight seconds. For very
   large objects, an admin-owned wrapper can pass fetch --seconds N; this
   also raises the delay before ordinary HTTP fallback.

## abuild sources

APKBUILD source downloads use the same catalog and helper in the
[abuild fork](https://github.com/Spaceghost/abuild). Publish entries for the
resolved HTTP(S) source URLs using the exact immutable bytes. For a
DISTFILES_MIRROR, add catalog entries for its URLs if you want those requests
to use the swarm. Configure ABUILD_WEBSEED_FETCHER in abuild.conf or export
it in the builder environment, along with APK_WEBSEED_CATALOG and
APK_WEBSEED_CACHE.

The abuild fork uses the swarm when the APKBUILD already declares a SHA-512
or legacy SHA-256 checksum. The helper checks it before handing the file to
abuild-fetch, and the normal abuild verification still runs. Initial
checksum generation and non-HTTP(S) sources follow the stock path.

## Pilot measurement

Bak seeded a 12 MiB fixture to two concurrent cold clients on Alienware
over their wired LAN. The throttle was shared across HTTP connections to
model a bandwidth-limited origin. Both clients finished in 4.98 s with
the seeder available and in 10.54 s with HTTP alone. The webseed served
5.63 MiB of the 24 MiB delivered in the seeded run. All outputs had the
same SHA-256. A single HTTP-only transfer took 5.25 s.
