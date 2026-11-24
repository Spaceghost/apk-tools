# apk-tools

Alpine Package Keeper (apk) is a package manager originally built for Alpine Linux,
but now used by several other distributions as well.

## Building

The preferred build system for building apk-tools is Meson:

```
# meson setup -Dprefix=/ build
# ninja -C build
# meson install -C build
```

For bootstrapping without Python, muon is also compatible. All you have to do is replace `meson` with `muon` in the above example.

To build a static apk, pass the right arguments to the above commands:

```
# meson setup -Dc_link_args="-static" -Dprefer_static=true -Ddefault_library=static build
# ninja -C build src/apk
```

Which will give you a `./build/src/apk` that is statically linked.

## Documentation

Online documentation is available in the [doc/](doc/) directory in the form of man pages.

The [apk(8)](doc/apk.8.scd) man page provides a basic overview of the package management
system.

The Cascadia fork includes optional [LAN package delivery](doc/CASCADIA-WEBSEED.md).
