#include "nvenc-internal.h"
#include "nvenc-helpers.h"
#include "nvenc-upload.h"

/*
 * NVENC implementation using CUDA context and arrays
 */

/* ------------------------------------------------------------------------- */
/* CUDA Context management                                                   */

bool cuda_ctx_init(struct nvenc_data *enc, obs_data_t *settings, const bool texture)
{
#ifdef _WIN32
	if (texture) {
		return true;
	}
#endif

	int count;
	CUdevice device;

	int gpu = (int)obs_data_get_int(settings, "device");
#ifndef _WIN32
	/* CUDA can do fairly efficient cross-GPU OpenGL mappings, allow it as
	 * a hidden option for experimentation. */
	bool force_cuda_tex = obs_data_get_bool(settings, "force_cuda_tex");
#endif

	if (gpu == -1) {
		gpu = 0;
	}

	CU_FAILED(cu->cuInit(0))
	CU_FAILED(cu->cuDeviceGetCount(&count))
	if (!count) {
		NV_FAIL("No CUDA devices found");
		return false;
	}
#ifdef _WIN32
	CU_FAILED(cu->cuDeviceGet(&device, gpu))
#else
	if (!texture || force_cuda_tex) {
		CU_FAILED(cu->cuDeviceGet(&device, gpu))
	} else {
		unsigned int ctx_count = 0;
		CUdevice devices[2];

		obs_enter_graphics();
		CUresult res = cu->cuGLGetDevices(&ctx_count, devices, 2, CU_GL_DEVICE_LIST_ALL);
		obs_leave_graphics();

		if (res != CUDA_SUCCESS || !ctx_count) {
			/* Probably running on iGPU, should just fall back to
			 * non-texture encoder. */
			if (res == CUDA_ERROR_INVALID_GRAPHICS_CONTEXT) {
				info("Not running on NVIDIA GPU, falling back "
				     "to non-texture encoder");
			} else {
				const char *name, *desc;
				if (cuda_get_error_desc(res, &name, &desc)) {
					error("Failed to get a CUDA device for "
					      "the current OpenGL context: "
					      "%s: %s",
					      name, desc);
				} else {
					error("Failed to get a CUDA device for "
					      "the current OpenGL context: %d",
					      res);
				}
			}
			return false;
		}

		/* Documentation indicates this should only ever happen with
		 * SLI, i.e. never for OBS. */
		if (ctx_count > 1) {
			warn("Got more than one CUDA devices for OpenGL context,"
			     " this is untested.");
		}

		device = devices[0];
		debug("Loading up CUDA on device %u", device);
	}
#endif
	CU_FAILED(cu->cuCtxCreate(&enc->cu_ctx, 0, device))
	CU_FAILED(cu->cuCtxPopCurrent(NULL))

	return true;
}

void cuda_ctx_free(struct nvenc_data *enc)
{
	if (enc->cu_ctx) {
		cu->cuCtxPopCurrent(NULL);
		cu->cuCtxDestroy(enc->cu_ctx);
	}
}

/* ------------------------------------------------------------------------- */
/* CUDA Surface management                                                   */

static bool cuda_surface_init(struct nvenc_data *enc, struct nv_cuda_surface *nvsurf)
{
	const bool p010 = obs_encoder_video_tex_active(enc->encoder, VIDEO_FORMAT_P010);
	CUDA_ARRAY3D_DESCRIPTOR desc;
	desc.Width = enc->cx;
	desc.Height = enc->cy;
	desc.Depth = 0;
	desc.Flags = CUDA_ARRAY3D_SURFACE_LDST;
	desc.NumChannels = 1;

	if (!enc->non_texture) {
		desc.Format = p010 ? CU_AD_FORMAT_UNSIGNED_INT16 : CU_AD_FORMAT_UNSIGNED_INT8;
		desc.Height = enc->cy + enc->cy / 2;
	} else {
		switch (enc->surface_format) {
		case NV_ENC_BUFFER_FORMAT_NV12:
			desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
			// Additional half-height plane for UV data
			desc.Height += enc->cy / 2;
			break;
		case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
			desc.Format = CU_AD_FORMAT_UNSIGNED_INT16; // 2 bytes per element
			desc.Height += enc->cy / 2;
			break;
		case NV_ENC_BUFFER_FORMAT_YUV444:
			desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
			desc.Height *= 3; // 3 full-size planes
			break;
		default:
			error("Unknown input format: %d", enc->surface_format);
			return false;
		}
	}

	CU_FAILED(cu->cuArray3DCreate(&nvsurf->tex, &desc))

	NV_ENC_REGISTER_RESOURCE res = {0};
	res.version = NV_ENC_REGISTER_RESOURCE_VER;
	res.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDAARRAY;
	res.resourceToRegister = (void *)nvsurf->tex;
	res.width = enc->cx;
	res.height = enc->cy;
	res.pitch = (uint32_t)(desc.Width * desc.NumChannels);
	if (!enc->non_texture) {
		res.bufferFormat = p010 ? NV_ENC_BUFFER_FORMAT_YUV420_10BIT : NV_ENC_BUFFER_FORMAT_NV12;
	} else {
		res.bufferFormat = enc->surface_format;
	}

	if (NV_FAILED(nv.nvEncRegisterResource(enc->session, &res))) {
		return false;
	}

	nvsurf->res = res.registeredResource;
	nvsurf->mapped_res = NULL;
	return true;
}

bool cuda_init_surfaces(struct nvenc_data *enc)
{
	switch (enc->in_format) {
	case VIDEO_FORMAT_P010:
		enc->surface_format = NV_ENC_BUFFER_FORMAT_YUV420_10BIT;
		break;
	case VIDEO_FORMAT_I444:
		enc->surface_format = NV_ENC_BUFFER_FORMAT_YUV444;
		break;
	default:
		enc->surface_format = NV_ENC_BUFFER_FORMAT_NV12;
	}

	da_reserve(enc->surfaces, enc->buf_count);

	CU_FAILED(cu->cuCtxPushCurrent(enc->cu_ctx))
	for (uint32_t i = 0; i < enc->buf_count; i++) {
		struct nv_cuda_surface buf;
		if (!cuda_surface_init(enc, &buf)) {
			return false;
		}

		da_push_back(enc->surfaces, &buf);
	}
	CU_FAILED(cu->cuCtxPopCurrent(NULL))

	return true;
}

static void cuda_surface_free(struct nvenc_data *enc, struct nv_cuda_surface *nvsurf)
{
	if (nvsurf->res) {
		if (nvsurf->mapped_res) {
			nv.nvEncUnmapInputResource(enc->session, nvsurf->mapped_res);
		}
		nv.nvEncUnregisterResource(enc->session, nvsurf->res);
		cu->cuArrayDestroy(nvsurf->tex);
	}
}

void cuda_free_surfaces(struct nvenc_data *enc)
{
	if (!enc->cu_ctx) {
		return;
	}

	cu->cuCtxPushCurrent(enc->cu_ctx);
	for (size_t i = 0; i < enc->surfaces.num; i++) {
		cuda_surface_free(enc, &enc->surfaces.array[i]);
	}
	if (enc->upload_buffer) {
		cu->cuMemHostUnregister(enc->upload_buffer);
		bfree(enc->upload_buffer);
		enc->upload_buffer = NULL;
		enc->upload_buffer_size = 0;
	}
	cu->cuCtxPopCurrent(NULL);
}

/* ------------------------------------------------------------------------- */
/* Actual encoding stuff                                                     */

static inline bool copy_frame(struct nvenc_data *enc, struct encoder_frame *frame, struct nv_cuda_surface *surf)
{
	bool success = true;
	CU_FAILED(cu->cuCtxPushCurrent(enc->cu_ctx))

	/* Keep one owned, page-locked staging buffer per encoder instead of
	 * registering/unregistering every plane of every frame. cuMemcpy2D is
	 * synchronous, so the next frame may safely reuse this buffer. */
	if (!enc->upload_buffer) {
		size_t size = nvenc_upload_size(enc->cx, enc->cy, enc->in_format);
		if (!size) {
			success = false;
			goto unmap;
		}
		uint8_t *buffer = bmalloc(size);
		if (!cuda_error_check(enc, cu->cuMemHostRegister(buffer, size, 0), __FUNCTION__, "cuMemHostRegister")) {
			bfree(buffer);
			success = false;
			goto unmap;
		}
		enc->upload_buffer = buffer;
		enc->upload_buffer_size = size;
	}
	if (!nvenc_pack_frame(enc->upload_buffer, enc->upload_buffer_size, frame, enc->cx, enc->cy, enc->in_format)) {
		error("Invalid frame layout for CUDA upload");
		success = false;
		goto unmap;
	}

	CUDA_MEMCPY2D m = {0};
	m.srcMemoryType = CU_MEMORYTYPE_HOST;
	m.srcHost = enc->upload_buffer;
	m.srcPitch = (size_t)enc->cx * (enc->in_format == VIDEO_FORMAT_P010 ? 2 : 1);
	m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
	m.dstArray = surf->tex;
	m.WidthInBytes = m.srcPitch;
	m.Height = enc->upload_buffer_size / m.srcPitch;
	CU_CHECK(cu->cuMemcpy2D(&m))

unmap:
	CU_FAILED(cu->cuCtxPopCurrent(NULL))

	return success;
}

bool cuda_encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet)
{
	struct nvenc_data *enc = data;
	struct nv_cuda_surface *surf;
	struct nv_bitstream *bs;

	bs = &enc->bitstreams.array[enc->next_bitstream];
	surf = &enc->surfaces.array[enc->next_bitstream];

	deque_push_back(&enc->dts_list, &frame->pts, sizeof(frame->pts));

	/* ------------------------------------ */
	/* copy to CUDA surface                 */

	if (!copy_frame(enc, frame, surf)) {
		return false;
	}

	/* ------------------------------------ */
	/* map output tex so nvenc can use it   */

	NV_ENC_MAP_INPUT_RESOURCE map = {NV_ENC_MAP_INPUT_RESOURCE_VER};
	map.registeredResource = surf->res;
	map.mappedBufferFmt = enc->surface_format;

	if (NV_FAILED(nv.nvEncMapInputResource(enc->session, &map))) {
		return false;
	}

	surf->mapped_res = map.mappedResource;

	/* ------------------------------------ */
	/* do actual encode call                */

	return nvenc_encode_base(enc, bs, surf->mapped_res, frame->pts, packet, received_packet);
}
