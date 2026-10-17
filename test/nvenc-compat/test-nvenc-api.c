#include "nvenc-api-version.h"

#include <stdio.h>

#define CHECK(condition)                                                              \
	do {                                                                          \
		if (!(condition)) {                                                    \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			return 1;                                                      \
		}                                                                     \
	} while (0)

#define DECLARE_TEST(name, version) int test_nvenc_abi_##name(void);
NVENC_API_BACKENDS(DECLARE_TEST)
#undef DECLARE_TEST

int main(void)
{
	static const struct {
		uint32_t driver;
		uint32_t expected;
	} cases[] = {{0, 0},       {0xb1, 0},    {0xc0, 0},    {0xc1, 0xc1}, {0xc2, 0xc2},      {0xc3, 0xc2},
		     {0xcf, 0xc2}, {0xd0, 0xd0}, {0xd1, 0xd0}, {0xe0, 0xd0}, {UINT32_MAX, 0xd0}};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
		CHECK(nvenc_select_api(cases[i].driver) == cases[i].expected);
	puts("PASS API selection: 12.1/12.2/13.0, unsupported old and future drivers");

	CHECK(nvenc_api_supports_split_mode(0xc1, 15));
	CHECK(nvenc_api_supports_split_mode(0xc2, 15));
	CHECK(nvenc_api_supports_split_mode(0xd0, 15));
	CHECK(nvenc_api_supports_split_mode(0xc1, 3));
	CHECK(!nvenc_api_supports_split_mode(0xc1, 4));
	CHECK(!nvenc_api_supports_split_mode(0xc2, 4));
	CHECK(nvenc_api_supports_split_mode(0xd0, 4));
	CHECK(!nvenc_api_supports_split_mode(0xd0, -1));
	CHECK(!nvenc_api_supports_split_mode(0xd0, 5));
	puts("PASS version-gated split encoding including disabled mode 15");

#define RUN_TEST(name, version) CHECK(test_nvenc_abi_##name() == 0);
	NVENC_API_BACKENDS(RUN_TEST)
#undef RUN_TEST
	puts("ALL NVENC API COMPATIBILITY TESTS PASSED");
	return 0;
}
