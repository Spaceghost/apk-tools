#include <stdio.h>
#include <unistd.h>
#include <cmocka.h>
#include "apk_test.h"
#include "apk_crypto.h"

static int num_tests;
static struct CMUnitTest all_tests[100];

void test_register(const char *name, UnitTestFunction f)
{
	all_tests[num_tests++] = (struct CMUnitTest) {
		.name = name,
		.test_func = f,
	};
}

int main(void)
{
	if (access("test/crypto", F_OK) == 0) chdir("test/crypto");
	apk_crypto_init();
	return _cmocka_run_group_tests("crypto_tests", all_tests, num_tests, NULL, NULL);
}
