#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Driver-reported versions use major << 4 | minor, not NVENCAPI_VERSION. */
#define NVENC_API_BACKENDS(X) \
	X(13_0, 0xd0)        \
	X(12_2, 0xc2)        \
	X(12_1, 0xc1)

static inline uint32_t nvenc_select_api(uint32_t driver_version)
{
#define SELECT_API(name, version) \
	if (driver_version >= version) \
		return version;
	NVENC_API_BACKENDS(SELECT_API)
#undef SELECT_API
	return 0;
}

static inline bool nvenc_api_supports_split_mode(uint32_t api, int64_t mode)
{
	return (mode >= 0 && mode <= 3) || mode == 15 || (api >= 0xd0 && mode == 4);
}
