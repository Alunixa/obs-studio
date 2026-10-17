#include "../nvenc-api-version.h"

#include <ffnvcodec/dynlink_loader.h>
#include <chrono>
#include <future>

#define DECLARE_PROBE(name, version) extern "C" int nvenc_probe_##name(void);
NVENC_API_BACKENDS(DECLARE_PROBE)
#undef DECLARE_PROBE

struct probe_backend {
	uint32_t version;
	int (*run)(void);
};

#define PROBE(name, api) {api, nvenc_probe_##name},
static const probe_backend backends[] = {NVENC_API_BACKENDS(PROBE)};
#undef PROBE

static int check_thread()
{
	NvencFunctions *functions = nullptr;
	uint32_t driver_version = 0;
	if (nvenc_load_functions(&functions, nullptr)) {
		printf("[general]\nreason=nvenc_lib\nnvenc_supported=false\n");
		return 1;
	}
	NVENCSTATUS status = functions->NvEncodeAPIGetMaxSupportedVersion(&driver_version);
	nvenc_free_functions(&functions);
	if (status != NV_ENC_SUCCESS) {
		printf("[general]\nreason=no_nvenc_version\nnvenc_supported=false\n");
		return 1;
	}

	uint32_t api = nvenc_select_api(driver_version);
	for (const auto &backend : backends) {
		if (backend.version == api) {
			return backend.run();
		}
	}

	printf("[general]\nnvenc_ver=%u.%u\nreason=outdated_driver\nnvenc_supported=false\n", driver_version >> 4,
	       driver_version & 0xf);
	return 1;
}

int main(int, char **)
{
	auto result = std::async(std::launch::async, check_thread);
	if (result.wait_for(std::chrono::milliseconds(2500)) == std::future_status::timeout) {
		exit(1);
	}
	return result.get();
}
