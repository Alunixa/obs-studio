#include <obs-module.h>

#include "obs-nvenc.h"
#include "nvenc-api-version.h"

struct nvenc_backend {
	uint32_t version;
	bool (*supported)(void);
	void (*load)(void);
	void (*unload)(void);
	void (*cuda_load)(void);
	void (*cuda_unload)(void);
};

#define DECLARE_BACKEND(name, version)       \
	bool nvenc_supported_##name(void);   \
	void obs_nvenc_load_##name(void);    \
	void obs_nvenc_unload_##name(void);  \
	void obs_cuda_load_##name(void);     \
	void obs_cuda_unload_##name(void);
NVENC_API_BACKENDS(DECLARE_BACKEND)
#undef DECLARE_BACKEND

#define BACKEND(name, api)                                                                                      \
	{api, nvenc_supported_##name, obs_nvenc_load_##name, obs_nvenc_unload_##name, obs_cuda_load_##name,        \
	 obs_cuda_unload_##name},
static const struct nvenc_backend backends[] = {NVENC_API_BACKENDS(BACKEND)};
#undef BACKEND

static const struct nvenc_backend *active_backend;

static uint32_t driver_api_version(void)
{
#if defined(_M_ARM64)
	void *lib = os_dlopen("nvEncodeAPI.dll");
#elif defined(_WIN32)
	void *lib = os_dlopen("nvEncodeAPI64.dll");
#else
	void *lib = os_dlopen("libnvidia-encode.so.1");
#endif
	if (!lib)
		return 0;

#ifdef _WIN32
	typedef int(__stdcall * max_version_func)(uint32_t *);
#else
	typedef int (*max_version_func)(uint32_t *);
#endif
	max_version_func max_version = (max_version_func)os_dlsym(lib, "NvEncodeAPIGetMaxSupportedVersion");
	uint32_t version = 0;
	if (!max_version || max_version(&version) != 0)
		version = 0;
	os_dlclose(lib);
	return version;
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-nvenc", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "NVIDIA Encoder (NVENC) Plugin";
}

bool obs_module_load(void)
{
	const uint32_t driver_version = driver_api_version();
	const uint32_t api = nvenc_select_api(driver_version);
	for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++) {
		if (backends[i].version != api)
			continue;
		if (!backends[i].supported()) {
			blog(LOG_INFO, "NVENC not supported");
			return false;
		}
		active_backend = &backends[i];
		active_backend->load();
		active_backend->cuda_load();
		return true;
	}

	blog(LOG_INFO, "[obs-nvenc] Driver NVENC API %u.%u is unavailable or below the supported minimum 12.1",
	     driver_version >> 4, driver_version & 0xf);
	return false;
}

void obs_module_unload(void)
{
	if (active_backend) {
		active_backend->cuda_unload();
		active_backend->unload();
		active_backend = NULL;
	}
}
