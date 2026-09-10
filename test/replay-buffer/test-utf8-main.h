#pragma once

#include <util/bmem.h>
#include <util/platform.h>

/* The OBS APIs consume UTF-8. Windows narrow argv uses the system code page,
 * which would test a different directory for non-ASCII user names/paths. */
#ifdef _WIN32
int wmain(int argc, wchar_t **wide_argv)
{
	char **argv = bzalloc(((size_t)argc + 1) * sizeof(char *));
	for (int i = 0; i < argc; i++) {
		CHECK(os_wcs_to_utf8_ptr(wide_argv[i], 0, &argv[i]) != 0);
	}
	int result = test_main(argc, argv);
	for (int i = 0; i < argc; i++) {
		bfree(argv[i]);
	}
	bfree(argv);
	return result;
}
#else
int main(int argc, char **argv)
{
	return test_main(argc, argv);
}
#endif
