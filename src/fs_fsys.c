/* fsops_sys.c - Alpine Package Keeper (APK)
 *
 * Copyright (C) 2005-2008 Natanael Copa <n@tanael.org>
 * Copyright (C) 2008-2011 Timo Teräs <timo.teras@iki.fi>
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "apk_fs.h"
#include "apk_xattr.h"
#include "apk_extract.h"
#include "apk_database.h" // for db->atoms

#ifndef O_PATH
#define O_PATH			0
#endif

#define TMPNAME_MAX		64 // ".apk." + 48 hex digits + 0

#define DIRFD_MAX_LOOPS		40

struct dirfd_resolver {
	struct apk_dirfd_cache *c;
	int dest_fd;
	int protect_fd;
	int loops;
};

struct dirfd_cache_entry {
	apk_blob_t path;
	int fd;
	unsigned int tick;
	unsigned int depth;
};

struct apk_dirfd_cache {
	unsigned int tick;
	int nentries;
	struct dirfd_cache_entry entry[32];
};

void apk_fs_flush_cache(struct apk_ctx *ac, bool free_cache)
{
	struct apk_dirfd_cache *c = ac->dirfd_cache;
	if (!c) return;
	for (int i = 0; i < c->nentries; i++) {
		close(c->entry[i].fd);
		free(c->entry[i].path.ptr);
	}
	c->nentries = 0;
	if (free_cache) {
		free(c);
		ac->dirfd_cache = NULL;
	}
}

static struct apk_dirfd_cache *dirfd_cache_get(struct apk_ctx *ac)
{
	struct apk_dirfd_cache *c = ac->dirfd_cache;

	if (!c) {
		c = calloc(1, sizeof *c);
		if (!c) return NULL;
		ac->dirfd_cache = c;
	}
	return c;
}

// Seed from the longest cached proper prefix of path; *curpos is set to
// its length. Exact matches are handled by the caller.
static int dirfd_cache_seed(struct dirfd_resolver *dr, apk_blob_t path, int *curpos)
{
	struct apk_dirfd_cache *c = dr->c;
	int seedlen = 0;
	struct dirfd_cache_entry *seed = NULL;

	for (int i = 0; i < c->nentries; i++) {
		struct dirfd_cache_entry *e = &c->entry[i];
		int l = e->path.len;
		if (l < path.len && path.ptr[l] == '/' &&
		    apk_blob_starts_with(path, e->path) && l >= seedlen) {
			seed = e;
			seedlen = l;
		}
	}
	if (seed) {
		seed->tick = ++c->tick;
		*curpos = seedlen;
		return seed->fd;
	}
	*curpos = 0;
	return dr->dest_fd;
}

static int dirfd_cache_add(struct apk_dirfd_cache *c, apk_blob_t key, int fd, int protect_fd)
{
	char *path;
	struct dirfd_cache_entry *e = NULL;
	int i;

	const int64_t depth_bias = 1024;
	int64_t score = 0;
	for (i = 0; i < c->nentries; i++) {
		struct dirfd_cache_entry *cur = &c->entry[i];
		if (cur->fd != protect_fd) {
			int64_t s = (int64_t)cur->tick - depth_bias * cur->depth;
			if (e == NULL || s < score) {
				e = cur;
				score = s;
			}
		}
		if (apk_blob_compare(cur->path, key) == 0) {
			cur->tick = ++c->tick;
			close(fd);
			return cur->fd;
		}
	}

	path = malloc(key.len);
	if (!path) {
		close(fd);
		return -ENOMEM;
	}
	memcpy(path, key.ptr, key.len);

	if (c->nentries < ARRAY_SIZE(c->entry)) {
		e = &c->entry[c->nentries++];
	} else {
		close(e->fd);
		free(e->path.ptr);
	}
	e->path = APK_BLOB_PTR_LEN(path, key.len);
	e->fd = fd;
	e->tick = ++c->tick;
	e->depth = 1;
	for (i = 0; i < key.len; i++)
		e->depth += (key.ptr[i] == '/');
	return fd;
}

static int dirfd_cache_lookup(struct apk_dirfd_cache *c, apk_blob_t key)
{
	for (int i = 0; i < c->nentries; i++) {
		struct dirfd_cache_entry *e = &c->entry[i];
		if (apk_blob_compare(e->path, key) != 0)
			continue;
		e->tick = ++c->tick;
		return e->fd;
	}
	return -1;
}

// Deferred symlink ownership checks: each fires when the walk reaches the
// position the target resolves to.
struct dirfd_pending_check {
	int num;
	uid_t uid[DIRFD_MAX_LOOPS];
	int pos[DIRFD_MAX_LOOPS];
};

static int dirfd_fire_checks(struct dirfd_pending_check *p, int curfd, int curpos)
{
	struct stat st;
	int i, j = 0;

	for (i = 0; i < p->num; i++) {
		if (p->pos[i] > curpos) {
			p->uid[j] = p->uid[i];
			p->pos[j] = p->pos[i];
			j++;
			continue;
		}
		if (fstat(curfd, &st) != 0) return -errno;
		if (p->uid[i] != st.st_uid) return -EPERM;
	}
	p->num = j;
	return 0;
}

// Map check positions across a path rewrite.
static void dirfd_move_checks(struct dirfd_pending_check *p, int pos, int delta)
{
	for (int i = 0; i < p->num; i++)
		if (p->pos[i] >= pos) p->pos[i] += delta;
}

static int dirfd_walk(struct dirfd_resolver *dr, apk_blob_t path);

// Shared resolver keeps the symlink loop budget global across nested walks.
static int dirfd_resolve(struct dirfd_resolver *dr, apk_blob_t key)
{
	if (key.len == 0) return dr->dest_fd;

	int fd = dirfd_cache_lookup(dr->c, key);
	if (fd >= 0) return fd;

	fd = dirfd_walk(dr, key);
	if (fd < 0) return fd;

	return dirfd_cache_add(dr->c, key, fd, dr->protect_fd);
}

// Resolve 'path' with resolve-in-root semantics. The cache assumes the tree
// is not concurrently modified; user planted symlinks are never followed into
// a more privileged directory.
//
// The walk only hands walk-owned fds to the cache ('owned' guards every
// dirfd_cache_add), never holds a borrow across one, never returns or
// closes a borrow; on error only owned fds are closed.
static int dirfd_walk(struct dirfd_resolver *dr, apk_blob_t path)
{
	struct apk_dirfd_cache *c = dr->c;
	int dest_fd = dr->dest_fd;
	char buf[PATH_MAX];		// walk path, rewritten in place
	char target[PATH_MAX];		// symlink target, does not alias buf
	struct dirfd_pending_check pending;
	struct stat st;
	int explen, curpos, curfd;
	bool owned = false;
	int r = 0, nfd;

	if (path.len >= PATH_MAX) return -ENAMETOOLONG;
	memcpy(buf, path.ptr, path.len);
	explen = path.len;

	curfd = dirfd_cache_seed(dr, APK_BLOB_PTR_LEN(buf, explen), &curpos);
	pending.num = 0;

	while (curpos < explen) {
		int s = curpos + (curpos > 0);
		char *sep = apk_blob_chr(APK_BLOB_PTR_LEN(buf + s, explen - s), '/');
		int e = sep ? sep - buf : explen;
		apk_blob_t comp = APK_BLOB_PTR_LEN(buf + s, e - s);

		// '.', empty components and '..' at the root are transparent;
		// cut them out, keeping the path canonical.
		if (comp.len == 0 || apk_blob_compare(comp, APK_BLOB_STRLIT(".")) == 0 ||
		    (curpos == 0 && apk_blob_compare(comp, APK_BLOB_STRLIT("..")) == 0)) {
			int start = (s > 0) ? s - 1 : 0;
			int end = (e > s || s > 0) ? e : e + 1;
			dirfd_move_checks(&pending, end, start - end);
			memmove(buf + start, buf + end, explen - end);
			explen += start - end;
			r = dirfd_fire_checks(&pending, curfd, curpos);
			if (r < 0) goto err;
			continue;
		}

		// '..' pops the last component: the parent fd comes from the
		// resolver - cache, dest_fd for the empty prefix, or a nested
		// walk of the prefix.
		if (apk_blob_compare(comp, APK_BLOB_STRLIT("..")) == 0) {
			apk_blob_t parent = APK_BLOB_PTR_LEN(buf, 0);
			apk_blob_rsplit(APK_BLOB_PTR_LEN(buf, curpos), '/', &parent, NULL);
			int t = parent.len;
			// Hand the popped fd to the cache before dropping it.
			if (owned) {
				dirfd_cache_add(c, APK_BLOB_PTR_LEN(buf, curpos), curfd, dr->protect_fd);
				owned = false;
			}
			nfd = dirfd_resolve(dr, parent);
			if (nfd < 0) {
				r = nfd;
				goto err;
			}
			// Splice out the popped component and the '..'.
			int newlen = t + (explen - e);
			dirfd_move_checks(&pending, e, newlen - explen);
			memmove(buf + t, buf + e, explen - e);
			explen = newlen;
			curfd = nfd;
			curpos = t;
			r = dirfd_fire_checks(&pending, curfd, curpos);
			if (r < 0) goto err;
			continue;
		}

		// Terminate the component in place for the syscalls; longer
		// names cannot exist.
		if (comp.len > NAME_MAX) {
			r = -ENAMETOOLONG;
			goto err;
		}
		buf[e] = '\0';

		bool is_link = false;
		nfd = openat(curfd, buf + s, O_PATH | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
		if (nfd >= 0) {
			// O_PATH|O_NOFOLLOW opens symlinks themselves; check explicitly.
			if (fstat(nfd, &st) != 0) {
				r = -errno;
				close(nfd);
				goto err;
			}
			if (S_ISLNK(st.st_mode)) {
				close(nfd);
				is_link = true;
			} else if (!S_ISDIR(st.st_mode)) {
				// Only directories are walkable.
				r = -ENOTDIR;
				close(nfd);
				goto err;
			}
		} else if (errno == ELOOP) {
			// Portability fallback if O_PATH is defined zero
			if (fstatat(curfd, buf + s, &st, AT_SYMLINK_NOFOLLOW) != 0) {
				r = -errno;
				goto err;
			}
			is_link = true;
		} else {
			r = -errno;
			goto err;
		}

		if (is_link) {
			ssize_t l = readlinkat(curfd, buf + s, target, sizeof target);
			if (l < 0) { r = -errno; goto err; }
			if (l >= (ssize_t)sizeof target) { r = -ENAMETOOLONG; goto err; }
			if (++dr->loops > DIRFD_MAX_LOOPS) { r = -ELOOP; goto err; }
			if (e < explen) buf[e] = '/';

			// Trailing slashes do not affect dir resolution; strip
			// them so the spliced text ends at a component boundary.
			apk_blob_t tgt = APK_BLOB_PTR_LEN(target, l);
			bool absolute = (tgt.len > 0 && tgt.ptr[0] == '/');
			tgt = apk_blob_trim_end(tgt, '/');

			int newlen, land;
			if (absolute) {
				// Re-root at the destination root, failing
				// closed when absent there.
				int tlen = (tgt.len > 0) ? tgt.len - 1 : 0;
				int restlen = (e < explen) ? explen - (e + 1) : 0;
				newlen = tlen + ((restlen) ? 1 + restlen : 0);
				if (newlen >= PATH_MAX) { r = -ENAMETOOLONG; goto err; }
				if (owned)
					dirfd_cache_add(c, APK_BLOB_PTR_LEN(buf, curpos), curfd, dr->protect_fd);
				if (restlen) memmove(buf + tlen + 1, buf + e + 1, restlen);
				memcpy(buf, tgt.ptr + 1, tlen);
				if (restlen) buf[tlen] = '/';
				land = tlen;
				curfd = dest_fd;
				owned = false;
				curpos = 0;
			} else {
				// Empty target: resolution fails, like the kernel.
				if (tgt.len == 0) { r = -ENOENT; goto err; }
				// Splice in place of the link; curfd remains the target's parent.
				int restlen = explen - e;
				newlen = s + tgt.len + restlen;
				if (newlen >= PATH_MAX) { r = -ENAMETOOLONG; goto err; }
				if (restlen) memmove(buf + s + tgt.len, buf + e, restlen);
				memcpy(buf + s, tgt.ptr, tgt.len);
				land = s + tgt.len;
			}
			dirfd_move_checks(&pending, e, newlen - explen);
			explen = newlen;

			if (st.st_uid != 0) {
				if (pending.num >= DIRFD_MAX_LOOPS) { r = -ELOOP; goto err; }
				pending.uid[pending.num] = st.st_uid;
				pending.pos[pending.num] = land;
				pending.num++;
			}
			r = dirfd_fire_checks(&pending, curfd, curpos);
			if (r < 0) goto err;
			continue;
		}

		// Descend, cache the previous position so later resolutions seed from it.
		if (e < explen) buf[e] = '/';
		if (owned) dirfd_cache_add(c, APK_BLOB_PTR_LEN(buf, curpos), curfd, dr->protect_fd);
		curfd = nfd;
		owned = true;
		curpos = e;
		r = dirfd_fire_checks(&pending, curfd, curpos);
		if (r < 0) goto err;
	}
	r = dirfd_fire_checks(&pending, curfd, curpos);
	if (r < 0) goto err;
	if (!owned) {
		// Empty path or only dots.
		curfd = openat(curfd, ".", O_PATH | O_DIRECTORY | O_CLOEXEC);
		if (curfd < 0) return -errno;
	}
	return curfd;

err:
	if (owned) close(curfd);
	return r;
}

static int dirfd_cache_resolve(struct apk_ctx *ac, apk_blob_t dir, int protect_fd)
{
	struct apk_dirfd_cache *c = dirfd_cache_get(ac);
	if (!c) return -errno;

	struct dirfd_resolver dr = {
		.c = c,
		.dest_fd = apk_ctx_fd_dest(ac),
		.protect_fd = protect_fd,
	};
	return dirfd_resolve(&dr, dir);
}

static bool dirfd_path_is_or_under(apk_blob_t path, apk_blob_t dir)
{
	return path.len >= dir.len
		&& apk_blob_starts_with(path, dir)
		&& (path.len == dir.len || path.ptr[dir.len] == '/');
}

static void dirfd_cache_drop(struct apk_dirfd_cache *c, apk_blob_t dir)
{
	for (int i = c->nentries - 1; i >= 0; i--) {
		struct dirfd_cache_entry *e = &c->entry[i];
		if (dirfd_path_is_or_under(e->path, dir)) {
			close(e->fd);
			free(e->path.ptr);
			c->entry[i] = c->entry[--c->nentries];
		}
	}
}

static int fsys_fixup_permissions(int atfd, const char *pathname, mode_t mode, uid_t uid, gid_t gid, bool reset_mode, unsigned int extract_flags)
{
	int ret = 0;

	if (extract_flags & APK_FSEXTRACTF_NO_CHOWN) return 0;
	if (fchownat(atfd, pathname, uid, gid, AT_SYMLINK_NOFOLLOW) < 0) ret |= APK_EXTRACTW_OWNER;
	if ((reset_mode || (mode & 07000) != 0) && fchmodat(atfd, pathname, mode & 07777, AT_SYMLINK_NOFOLLOW) < 0) ret |= APK_EXTRACTW_PERMISSION;
	return ret;
}

static int fsys_parentfd(struct apk_fsdir *d, const char **name)
{
	apk_blob_t b = apk_pathbuilder_get(&d->pb), parent, bname;

	if (!apk_blob_rsplit(b, '/', &parent, &bname)) {
		parent = APK_BLOB_STRLIT("");
		bname = b;
	}
	*name = bname.ptr;
	return dirfd_cache_resolve(d->ac, parent, -1);
}

static int fsys_dir_create(struct apk_fsdir *d, mode_t mode, uid_t uid, gid_t gid)
{
	const char *name;
	int fd = fsys_parentfd(d, &name);
	if (fd < 0) return fd;
	if (mkdirat(fd, name, mode) < 0) return -errno;
	return fsys_fixup_permissions(fd, name, mode, uid, gid, false, d->extract_flags);
}

static int fsys_dir_delete(struct apk_fsdir *d)
{
	const char *name;
	int fd = fsys_parentfd(d, &name);
	if (fd < 0) return fd;
	if (unlinkat(fd, name, AT_REMOVEDIR) < 0) return -errno;
	dirfd_cache_drop(d->ac->dirfd_cache, apk_pathbuilder_get(&d->pb));
	return 0;
}

static int fsys_dir_check(struct apk_fsdir *d, mode_t mode, uid_t uid, gid_t gid)
{
	struct stat st;
	const char *name;
	int fd = fsys_parentfd(d, &name);
	if (fd < 0) return fd;
	if (fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
		return -errno;
	if (!S_ISDIR(st.st_mode)) return -ENOTDIR;
	if ((st.st_mode & 07777) != (mode & 07777) || st.st_uid != uid || st.st_gid != gid)
		return APK_FS_DIR_MODIFIED;
	return 0;
}

static int fsys_dir_update_perms(struct apk_fsdir *d, mode_t mode, uid_t uid, gid_t gid)
{
	const char *name;
	int fd = fsys_parentfd(d, &name);
	if (fd < 0) return fd;
	return fsys_fixup_permissions(fd, name, mode, uid, gid, true, d->extract_flags);
}

static char *format_tmpname(struct apk_digest_ctx *dctx, apk_blob_t pkgctx, apk_blob_t filename, char tmpname[static TMPNAME_MAX])
{
	struct apk_digest d;
	apk_blob_t b = APK_BLOB_PTR_LEN(tmpname, TMPNAME_MAX);

	apk_digest_ctx_reset_alg(dctx, APK_DIGEST_SHA256);
	apk_digest_ctx_update(dctx, pkgctx.ptr, pkgctx.len);
	apk_digest_ctx_update(dctx, filename.ptr, filename.len);
	apk_digest_ctx_final(dctx, &d);

	apk_blob_push_blob(&b, APK_BLOB_STRLIT(".apk."));
	apk_blob_push_hexdump(&b, APK_BLOB_PTR_LEN((char *)d.data, 24));
	apk_blob_push_blob(&b, APK_BLOB_PTR_LEN("", 1));

	return tmpname;
}

static const char *split_path(const char *path, apk_blob_t *dir, apk_blob_t *name)
{
	apk_blob_t b = APK_BLOB_STR(path);

	if (!apk_blob_rsplit(b, '/', dir, name)) {
		*dir = APK_BLOB_NULL;
		*name = b;
		return path;
	}
	return name->ptr;
}

static int is_system_xattr(const char *name)
{
	return strncmp(name, "user.", 5) != 0;
}

static int fsys_file_extract(struct apk_ctx *ac, const struct apk_file_info *fi, struct apk_istream *is, unsigned int extract_flags, apk_blob_t pkgctx)
{
	char tmpname_file[TMPNAME_MAX], tmpname_linktarget[TMPNAME_MAX];
	int fd, r = -1, ret = 0;
	apk_blob_t dir, name;
	const char *link_target = fi->link_target;
	const char *fn = split_path(fi->name, &dir, &name);

	int atfd = dirfd_cache_resolve(ac, dir, -1);
	if (atfd < 0) return atfd;

	if (pkgctx.ptr) fn = format_tmpname(&ac->dctx, pkgctx, name, tmpname_file);

	if (!S_ISDIR(fi->mode) && !(extract_flags & APK_FSEXTRACTF_NO_OVERWRITE)) {
		if (unlinkat(atfd, fn, 0) != 0 && errno != ENOENT) return -errno;
		if (!pkgctx.ptr) dirfd_cache_drop(ac->dirfd_cache, APK_BLOB_STR(fi->name));
	}

	switch (fi->mode & S_IFMT) {
	case S_IFREG:
		if (!link_target) {
			int flags = O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC | O_EXCL;
			int fd = openat(atfd, fn, flags, fi->mode & 07777);
			if (fd < 0) return -errno;

			struct apk_ostream *os = apk_ostream_to_fd(fd);
			if (IS_ERR(os)) return PTR_ERR(os);
			apk_stream_copy(is, os, fi->size, 0);
			r = apk_ostream_close(os);
			if (r < 0) {
				unlinkat(atfd, fn, 0);
				return r;
			}
		} else {
			// Link against the target's temporary name: it might
			// not have been committed to its final name yet.
			apk_blob_t b_link_target = APK_BLOB_STR(link_target);
			apk_blob_t ldir, lname;
			const char *lt = split_path(link_target, &ldir, &lname);
			int lfd;

			if (apk_fs_is_malicious_pathname(b_link_target)) return -APKE_ADB_SCHEMA;

			// atfd must survive the target directory resolution.
			lfd = dirfd_cache_resolve(ac, ldir, atfd);
			if (lfd < 0) return lfd;

			if (pkgctx.ptr) lt = format_tmpname(&ac->dctx, pkgctx, lname, tmpname_linktarget);
			r = linkat(lfd, lt, atfd, fn, 0);
			if (r < 0) {
				if (errno == ENOENT && pkgctx.ptr) return -APKE_DIR_ALIAS;
				return -errno;
			}
		}
		break;
	case S_IFLNK:
		if (symlinkat(link_target, atfd, fn) < 0) return -errno;
		if (!pkgctx.ptr) dirfd_cache_drop(ac->dirfd_cache, APK_BLOB_STR(fi->name));
		break;
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
		if (extract_flags & APK_FSEXTRACTF_NO_DEVICES) return -APKE_NOT_EXTRACTED;
		if (mknodat(atfd, fn, fi->mode, fi->device) < 0) return -errno;
		break;
	default:
		return -APKE_NOT_EXTRACTED;
	}

	ret |= fsys_fixup_permissions(atfd, fn, fi->mode, fi->uid, fi->gid, false, extract_flags);

	/* extract xattrs */
	if (!S_ISLNK(fi->mode) && fi->xattrs && apk_array_len(fi->xattrs) != 0) {
		r = 0;
		fd = openat(atfd, fn, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
		if (fd >= 0) {
			apk_array_foreach(xattr, fi->xattrs) {
				if ((extract_flags & APK_FSEXTRACTF_NO_SYS_XATTRS) && is_system_xattr(xattr->name))
					continue;
				if (apk_fsetxattr(fd, xattr->name, xattr->value.ptr, xattr->value.len) < 0)
					ret |= APK_EXTRACTW_XATTR;
			}
			close(fd);
		} else {
			ret |= APK_EXTRACTW_XATTR;
		}
	}

	if (!S_ISLNK(fi->mode)) {
		/* preserve modification time */
		struct timespec times[2];
		times[0].tv_sec  = times[1].tv_sec  = fi->mtime;
		times[0].tv_nsec = times[1].tv_nsec = 0;
		if (utimensat(atfd, fn, times, AT_SYMLINK_NOFOLLOW) != 0) ret |= APK_EXTRACTW_MTIME;
	}

	return ret;
}

static int fsys_file_pin(int atfd, const char *pathname, int ctrl)
{
	if (!(ctrl & APK_FS_CTRL_CLEAR_MASK)) return -1;
	return openat(atfd, pathname, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
}

static void fsys_file_clear_and_unpin(int fd, int ctrl)
{
	if (fd < 0) return;
	if (ctrl & APK_FS_CTRL_CLEAR_MODE) fchmod(fd, 0600);
#ifdef __linux__
	// Linux specific xattr allowing gaining capabilities
	if (ctrl & APK_FS_CTRL_CLEAR_XATTRS) apk_fremovexattr(fd, "security.capability");
#endif
	close(fd);
}

static int fsys_file_control(struct apk_fsdir *d, apk_blob_t filename, int ctrl)
{
	struct apk_ctx *ac = d->ac;
	char tmpname[TMPNAME_MAX], apknewname[NAME_MAX + 64];

	int dfd = dirfd_cache_resolve(d->ac, apk_pathbuilder_get(&d->pb), -1);
	if (dfd < 0) return dfd;

	int n = apk_pathbuilder_pushb(&d->pb, filename);
	if (n < 0) return n;

	const char *fn = apk_pathbuilder_name(&d->pb, n);
	int rc = 0, pinfd = -1;
	switch (ctrl & ~APK_FS_CTRL_CLEAR_MASK) {
	case APK_FS_CTRL_COMMIT:
		// rename tmpname -> realname
		pinfd = fsys_file_pin(dfd, fn, ctrl);
		if (renameat(dfd, format_tmpname(&ac->dctx, d->pkgctx, filename, tmpname),
			     dfd, fn) < 0) {
			rc = -errno;
			// ENOENT: an aliased duplicate already consumed the tmpname
			if (rc == -ENOENT && d->pkgctx.ptr) rc = -APKE_DIR_ALIAS;
			unlinkat(dfd, tmpname, 0);
		} else {
			dirfd_cache_drop(ac->dirfd_cache, apk_pathbuilder_get(&d->pb));
		}
		break;
	case APK_FS_CTRL_APKNEW:
		// rename tmpname -> realname.apk-new
		rc = apk_fmt(apknewname, sizeof apknewname, "%s%s", fn, ac->apknew_suffix);
		if (rc < 0) break;
		if (renameat(dfd, format_tmpname(&ac->dctx, d->pkgctx, filename, tmpname),
			     dfd, apknewname) < 0) {
			rc = -errno;
			if (rc == -ENOENT && d->pkgctx.ptr) rc = -APKE_DIR_ALIAS;
		}
		break;
	case APK_FS_CTRL_CANCEL:
		// unlink tmpname
		if (unlinkat(dfd, format_tmpname(&ac->dctx, d->pkgctx, filename, tmpname), 0) < 0)
			rc = -errno;
		break;
	case APK_FS_CTRL_DELETE:
		// unlink realname
		pinfd = fsys_file_pin(dfd, fn, ctrl);
		if (unlinkat(dfd, fn, 0) < 0)
			rc = -errno;
		else
			dirfd_cache_drop(ac->dirfd_cache, apk_pathbuilder_get(&d->pb));
		break;
	case APK_FS_CTRL_DELETE_APKNEW:
		// remove apknew (which may or may not exist)
		rc = apk_fmt(apknewname, sizeof apknewname, "%s%s", fn, ac->apknew_suffix);
		if (rc < 0) break;
		unlinkat(dfd, apknewname, 0);
		break;
	default:
		rc = -ENOSYS;
		break;
	}
	fsys_file_clear_and_unpin(pinfd, (rc < 0) ? 0 : ctrl);
	apk_pathbuilder_pop(&d->pb, n);
	return rc;
}

static int fsys_file_info(struct apk_fsdir *d, apk_blob_t filename,
			  unsigned int flags, struct apk_file_info *fi)
{
	struct apk_ctx *ac = d->ac;
	int n, dfd, r;

	dfd = dirfd_cache_resolve(d->ac, apk_pathbuilder_get(&d->pb), -1);
	if (dfd < 0) return dfd;

	n = apk_pathbuilder_pushb(&d->pb, filename);
	if (n < 0) return n;
	r = apk_fileinfo_get(dfd, apk_pathbuilder_name(&d->pb, n), flags, fi, &ac->db->atoms);
	apk_pathbuilder_pop(&d->pb, n);
	return r;
}

static const struct apk_fsdir_ops fsdir_ops_fsys = {
	.priority = APK_FS_PRIO_DISK,
	.dir_create = fsys_dir_create,
	.dir_delete = fsys_dir_delete,
	.dir_check = fsys_dir_check,
	.dir_update_perms = fsys_dir_update_perms,
	.file_extract = fsys_file_extract,
	.file_control = fsys_file_control,
	.file_info = fsys_file_info,
};

static const struct apk_fsdir_ops *apk_fsops_get(apk_blob_t dir)
{
	if (apk_blob_starts_with(dir, APK_BLOB_STRLIT("uvol")) && (dir.len == 4 || dir.ptr[4] == '/')) {
		extern const struct apk_fsdir_ops fsdir_ops_uvol;
		return &fsdir_ops_uvol;
	}
	return &fsdir_ops_fsys;
}

bool apk_fs_is_malicious_filename(apk_blob_t file)
{
	const uint8_t *ptr = (const uint8_t *) file.ptr;
	if (file.len > NAME_MAX) return true;
	for (int i = 0; i < file.len; i++) {
		if (ptr[i] < 0x20 || ptr[i] == '/' || ptr[i] == 0x7f) return true;
	}
	switch (file.len) {
	case 5+48: return apk_blob_starts_with(file, APK_BLOB_STRLIT(".apk."));
	case 2: if (ptr[1] != '.') break; // fallthrough
	case 1: if (ptr[0] != '.') break; // fallthrough
	case 0: return true;
	}
	return false;
}

bool apk_fs_is_malicious_pathname(apk_blob_t path)
{
	apk_blob_t dir;
	while (apk_blob_split(path, APK_BLOB_STRLIT("/"), &dir, &path))
		if (apk_fs_is_malicious_filename(dir)) return true;
	return path.len > 0 && apk_fs_is_malicious_filename(path);
}

static bool need_checksum(const struct apk_file_info *fi)
{
	switch (fi->mode & S_IFMT) {
	case S_IFDIR:
	case S_IFSOCK:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
		return false;
	default:
		if (fi->link_target) return false;
		return true;
	}
}

int apk_fs_extract(struct apk_ctx *ac, const struct apk_file_info *fi, struct apk_istream *is, unsigned int extract_flags, apk_blob_t pkgctx)
{
	if (fi->digest.alg == APK_DIGEST_NONE && need_checksum(fi)) return -APKE_FORMAT_OBSOLETE;
	if (S_ISDIR(fi->mode)) {
		struct apk_fsdir fsd;
		apk_fsdir_get(&fsd, APK_BLOB_STR((char*)fi->name), extract_flags, ac, pkgctx);
		return apk_fsdir_create(&fsd, fi->mode, fi->uid, fi->gid);
	} else {
		const struct apk_fsdir_ops *ops = apk_fsops_get(APK_BLOB_PTR_LEN((char*)fi->name, strnlen(fi->name, 5)));
		return ops->file_extract(ac, fi, is, extract_flags, pkgctx);
	}
}

void apk_fsdir_get(struct apk_fsdir *d, apk_blob_t dir, unsigned int extract_flags, struct apk_ctx *ac, apk_blob_t pkgctx)
{
	d->ac = ac;
	d->pkgctx = pkgctx;
	d->extract_flags = extract_flags;
	d->ops = apk_fsops_get(dir);
	apk_pathbuilder_setb(&d->pb, apk_blob_trim_end(dir, '/'));
}
