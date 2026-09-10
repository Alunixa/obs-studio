#pragma once

#include <obs.h>
#include <string.h>

static inline size_t nvenc_upload_size(uint32_t width, uint32_t height, enum video_format format)
{
	if (!width || !height ||
	    (format != VIDEO_FORMAT_I444 && format != VIDEO_FORMAT_NV12 && format != VIDEO_FORMAT_P010)) {
		return 0;
	}
	if (format != VIDEO_FORMAT_I444 && ((width | height) & 1)) {
		return 0;
	}
	uint64_t pitch = (uint64_t)width * (format == VIDEO_FORMAT_P010 ? 2 : 1);
	uint64_t rows = format == VIDEO_FORMAT_I444 ? (uint64_t)height * 3 : (uint64_t)height * 3 / 2;
	if (rows > SIZE_MAX / pitch) {
		return 0;
	}
	return (size_t)(pitch * rows);
}

static inline bool nvenc_pack_frame(uint8_t *dst, size_t capacity, const struct encoder_frame *frame, uint32_t width,
				    uint32_t height, enum video_format format)
{
	size_t required = nvenc_upload_size(width, height, format);
	if (!dst || !frame || !required || capacity < required) {
		return false;
	}

	size_t pitch = (size_t)width * (format == VIDEO_FORMAT_P010 ? 2 : 1);
	size_t planes = format == VIDEO_FORMAT_I444 ? 3 : 2;
	for (size_t plane = 0; plane < planes; plane++) {
		if (!frame->data[plane] || frame->linesize[plane] < pitch) {
			return false;
		}
	}
	for (size_t plane = 0; plane < planes; plane++) {
		size_t rows = plane && format != VIDEO_FORMAT_I444 ? height / 2 : height;
		if (frame->linesize[plane] == pitch) {
			memcpy(dst, frame->data[plane], pitch * rows);
			dst += pitch * rows;
		} else {
			for (size_t row = 0; row < rows; row++) {
				memcpy(dst, frame->data[plane] + row * frame->linesize[plane], pitch);
				dst += pitch;
			}
		}
	}
	return true;
}
