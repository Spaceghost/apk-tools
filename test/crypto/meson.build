if cmocka_dep.found()

crypto_test_exe = executable('crypto_tests',
	files(['crypto_test.c', 'main.c']),
	install: false,
	dependencies: [
		cmocka_dep,
		libapk_dep,
	],
	include_directories: include_directories('../unit'),
)

test('crypto_tests', crypto_test_exe,
	env : [
		'CMOCKA_MESSAGE_OUTPUT=XML',
		'CMOCKA_XML_FILE=' + meson.current_build_dir() + '/%g.junit.xml'
	],
	suite: 'crypto',
	workdir: meson.current_source_dir(),
	priority: 200)

endif
