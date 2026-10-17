#include "nvenc-api.h"

#include <stdio.h>
#include <stddef.h>

#define CHECK(condition)                                                              \
	do {                                                                          \
		if (!(condition)) {                                                    \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			return 1;                                                      \
		}                                                                     \
	} while (0)

#define API_RAW_VERSION ((OBS_NVENC_EXPECTED_VERSION >> 4) | ((OBS_NVENC_EXPECTED_VERSION & 0xf) << 24))
#define STRUCT_VERSION(revision) (API_RAW_VERSION | ((revision) << 16) | (0x7u << 28))
#define STRUCT_VERSION_HIGH(revision) (STRUCT_VERSION(revision) | (1u << 31))

static unsigned create_calls;
static unsigned open_calls;
static unsigned callback_failures;
static NVENCSTATUS create_status;
static NVENCSTATUS open_status;
static NV_ENC_DEVICE_TYPE expected_device_type;
static int device_marker;
static int session_marker;

static NVENCSTATUS NVENCAPI mock_open_session(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS *params, void **session)
{
	NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS expected = {0};
	expected.version = STRUCT_VERSION(1);
	expected.apiVersion = API_RAW_VERSION;
	expected.device = &device_marker;
	expected.deviceType = expected_device_type;
	open_calls++;
	if (memcmp(params, &expected, sizeof(expected)) != 0)
		callback_failures++;
	if (open_status == NV_ENC_SUCCESS)
		*session = &session_marker;
	return open_status;
}

static NVENCSTATUS NVENCAPI mock_create_instance(NV_ENCODE_API_FUNCTION_LIST *functions)
{
	NV_ENCODE_API_FUNCTION_LIST expected = {0};
	expected.version = STRUCT_VERSION(2);
	create_calls++;
	if (memcmp(functions, &expected, sizeof(expected)) != 0)
		callback_failures++;
	functions->nvEncOpenEncodeSessionEx = mock_open_session;
	return create_status;
}

int test_nvenc_abi(void)
{
	CHECK(NVENCAPI_VERSION == API_RAW_VERSION);
	CHECK(NV_ENCODE_API_FUNCTION_LIST_VER == STRUCT_VERSION(2));
	CHECK(NV_ENC_CAPS_PARAM_VER == STRUCT_VERSION(1));
	CHECK(NV_ENC_CREATE_BITSTREAM_BUFFER_VER == STRUCT_VERSION(1));
	CHECK(NV_ENC_MAP_INPUT_RESOURCE_VER == STRUCT_VERSION(4));
	CHECK(NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER == STRUCT_VERSION(1));

#if OBS_NVENC_EXPECTED_VERSION == 0xc1
	CHECK(NV_ENC_RECONFIGURE_PARAMS_VER == STRUCT_VERSION_HIGH(1));
	CHECK(NV_ENC_CONFIG_VER == STRUCT_VERSION_HIGH(8));
	CHECK(NV_ENC_INITIALIZE_PARAMS_VER == STRUCT_VERSION_HIGH(6));
	CHECK(NV_ENC_PRESET_CONFIG_VER == STRUCT_VERSION_HIGH(4));
	CHECK(NV_ENC_PIC_PARAMS_VER == STRUCT_VERSION_HIGH(6));
	CHECK(NV_ENC_LOCK_BITSTREAM_VER == STRUCT_VERSION_HIGH(1));
	CHECK(NV_ENC_REGISTER_RESOURCE_VER == STRUCT_VERSION(4));
#else
	CHECK(NV_ENC_RECONFIGURE_PARAMS_VER == STRUCT_VERSION_HIGH(2));
	CHECK(NV_ENC_CONFIG_VER == STRUCT_VERSION_HIGH(9));
	CHECK(NV_ENC_INITIALIZE_PARAMS_VER == STRUCT_VERSION_HIGH(7));
	CHECK(NV_ENC_PRESET_CONFIG_VER == STRUCT_VERSION_HIGH(5));
	CHECK(NV_ENC_PIC_PARAMS_VER == STRUCT_VERSION_HIGH(7));
	CHECK(NV_ENC_LOCK_BITSTREAM_VER == STRUCT_VERSION_HIGH(2));
	CHECK(NV_ENC_REGISTER_RESOURCE_VER == STRUCT_VERSION(5));
#endif

	NV_ENCODE_API_FUNCTION_LIST functions;
	memset(&functions, 0xa5, sizeof(functions));
	CHECK(nvenc_api_create_instance(mock_create_instance, OBS_NVENC_EXPECTED_VERSION - 1, &functions) ==
	      NV_ENC_ERR_INVALID_VERSION);
	CHECK(create_calls == 0);

	create_status = NV_ENC_SUCCESS;
	CHECK(nvenc_api_create_instance(mock_create_instance, OBS_NVENC_EXPECTED_VERSION, &functions) ==
	      NV_ENC_SUCCESS);
	CHECK(create_calls == 1);
	CHECK(callback_failures == 0);

	void *session = NULL;
	expected_device_type = NV_ENC_DEVICE_TYPE_CUDA;
	CHECK(nvenc_api_open_session(&functions, &device_marker, expected_device_type, &session) == NV_ENC_SUCCESS);
	CHECK(session == &session_marker);
	CHECK(open_calls == 1 && callback_failures == 0);

	expected_device_type = NV_ENC_DEVICE_TYPE_DIRECTX;
	CHECK(nvenc_api_open_session(&functions, &device_marker, expected_device_type, &session) == NV_ENC_SUCCESS);
	CHECK(open_calls == 2 && callback_failures == 0);

	session = NULL;
	open_status = NV_ENC_ERR_OUT_OF_MEMORY;
	CHECK(nvenc_api_open_session(&functions, &device_marker, expected_device_type, &session) ==
	      NV_ENC_ERR_OUT_OF_MEMORY);
	CHECK(session == NULL && open_calls == 3 && callback_failures == 0);

	create_status = NV_ENC_ERR_INVALID_VERSION;
	CHECK(nvenc_api_create_instance(mock_create_instance, 0xffff, &functions) == NV_ENC_ERR_INVALID_VERSION);
	CHECK(create_calls == 2 && callback_failures == 0);

	NV_ENC_PRESET_CONFIG preset;
	NV_ENC_PRESET_CONFIG expected = {0};
	expected.version = NV_ENC_PRESET_CONFIG_VER;
	expected.presetCfg.version = NV_ENC_CONFIG_VER;
	memset(&preset, 0xa5, sizeof(preset));
	nvenc_api_preset_config(&preset);
	CHECK(memcmp(&preset, &expected, sizeof(preset)) == 0);

	printf("PASS API %u.%u: native struct revisions, clean function table, CUDA/D3D11 sessions, preset ABI, errors\n",
	       OBS_NVENC_EXPECTED_VERSION >> 4, OBS_NVENC_EXPECTED_VERSION & 0xf);
	return 0;
}
