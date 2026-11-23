cc = meson.get_compiler('c')

libportability_src = []

check_symbols = [
	['getrandom', 'getrandom.c', 'NEED_GETRANDOM', 'sys/random.h'],
	['memrchr', 'memrchr.c', 'NEED_MEMRCHR', 'string.h'],
	['memfd_create', '', 'NEED_MEMFD_CREATE', 'sys/mman.h'],
	['mknodat', 'mknodat.c', 'NEED_MKNODAT', 'sys/stat.h'],
	['pipe2', 'pipe2.c', 'NEED_PIPE2', 'unistd.h'],
	['qsort_r', 'qsort_r.c', 'NEED_QSORT_R', 'stdlib.h'],
	['reallocarray', 'reallocarray.c', 'NEED_REALLOCARRAY', 'stdlib.h'],
	['strchrnul', 'strchrnul.c', 'NEED_STRCHRNUL', 'string.h'],
	['strlcpy', 'strlcpy.c', 'NEED_STRLCPY', 'string.h'],
	['SOCK_CLOEXEC', 'socket.c', 'NEED_SOCK_CLOEXEC', 'sys/socket.h'],
	['unshare', '', 'NEED_UNSHARE', 'sched.h'],
]

foreach f : check_symbols
	if not cc.has_header_symbol(f.get(3), f.get(0), args: ['-D_GNU_SOURCE'])
		add_project_arguments('-D' + f.get(2), language: 'c')
		compat_src = f.get(1)
		if compat_src != ''
			libportability_src += [compat_src]
		endif
	endif
endforeach

# Probe the qsort_r prototype variants, POSIX first.
qsort_r_prototypes = [
	['POSIX', 'void (*)(void *, size_t, size_t,\n\t\t\tint (*)(const void *, const void *, void *), void *)'],
	['BSD', 'void (*)(void *, size_t, size_t, void *,\n\t\t\tint (*)(void *, const void *, const void *))'],
]
qsort_r_test = '''
	#define _GNU_SOURCE
	#include <stdlib.h>
	_Static_assert(_Generic((qsort_r),
		@0@ : 1, default: 0),
		"@1@ prototype not matched");
'''

if cc.has_header_symbol('stdlib.h', 'qsort_r', args: ['-D_GNU_SOURCE'])
	qsort_r_variant = ''
	foreach p : qsort_r_prototypes
		test = qsort_r_test.format(p.get(1), p.get(0))
		if cc.compiles(test, name: 'Test qsort_r @0@ prototype'.format(p.get(0)))
			qsort_r_variant = p.get(0)
			break
		endif
	endforeach
	if qsort_r_variant == 'BSD'
		add_project_arguments('-DHAVE_BROKEN_QSORT_R', language: 'c')
	elif qsort_r_variant == ''
		error('Unrecognized qsort_r prototype')
	endif
endif

if libportability_src.length() > 0
	libportability = static_library(
		'portability',
		libportability_src,
	)

	libportability_dep = declare_dependency(
		link_whole: libportability,
		include_directories: include_directories('.'),
	)
else
	libportability_dep = declare_dependency(
		include_directories: include_directories('.'),
	)
endif
