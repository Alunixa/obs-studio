#pragma once

#include <stdint.h>
#include <string.h>
#include <ffnvcodec/nvEncodeAPI.h>

#define NVCODEC_CONFIGURED_VERSION ((NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION)

#if defined(OBS_NVENC_EXPECTED_VERSION) && OBS_NVENC_EXPECTED_VERSION != NVCODEC_CONFIGURED_VERSION
#error NVENC backend compiled with the wrong API headers
#endif

typedef NVENCSTATUS(NVENCAPI *NV_CREATE_INSTANCE_FUNC)(NV_ENCODE_API_FUNCTION_LIST *);

static inline NVENCSTATUS nvenc_api_create_instance(NV_CREATE_INSTANCE_FUNC create_instance, uint32_t driver_version,
						    NV_ENCODE_API_FUNCTION_LIST *functions)
{
	if (driver_version < NVCODEC_CONFIGURED_VERSION)
		return NV_ENC_ERR_INVALID_VERSION;

	memset(functions, 0, sizeof(*functions));
	functions->version = NV_ENCODE_API_FUNCTION_LIST_VER;
	return create_instance(functions);
}

static inline NVENCSTATUS nvenc_api_open_session(NV_ENCODE_API_FUNCTION_LIST *functions, void *device,
						 NV_ENC_DEVICE_TYPE device_type, void **session)
{
	NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = {0};
	params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
	params.apiVersion = NVENCAPI_VERSION;
	params.device = device;
	params.deviceType = device_type;
	return functions->nvEncOpenEncodeSessionEx(&params, session);
}

static inline void nvenc_api_preset_config(NV_ENC_PRESET_CONFIG *config)
{
	memset(config, 0, sizeof(*config));
	config->version = NV_ENC_PRESET_CONFIG_VER;
	config->presetCfg.version = NV_ENC_CONFIG_VER;
}
