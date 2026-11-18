apk-fix(8)

# NAME

apk fix - repair packages or system

# SYNOPSIS

*apk fix* [<_options_>...] [_packages_...]

# DESCRIPTION

*apk fix* applies repair strategies to correct errors in the installation of
the specified packages, or all installed packages if none are specified.

# OPTIONS

*apk fix* supports the commit options described in *apk*(8), as well as the
following options:

*--depends*, *-d*
	Also fix dependencies of specified packages.

*--directory-permissions*
	Reset all directory permissions.

*--reinstall*, *-r*
	Reinstall packages (default).

*--upgrade*, *-u*
	Upgrade name _PACKAGE_ if an upgrade exists and does not break
	dependencies.

*--xattr*, *-x*
	Fix packages with broken xattrs.

# EXAMPLES

\# reinstall a single package++
apk fix musl

\# reset all directory permissions++
apk fix --directory-permissions

\# reinstall all installed packages, e.g. after file system++
\# corruption; the star must be quoted to prevent shell expansion++
apk fix --reinstall '\*'
