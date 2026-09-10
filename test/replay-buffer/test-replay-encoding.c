#include <obs.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/threading.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                 \
	do {                                                                             \
		if (!(condition)) {                                                       \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			exit(1);                                                         \
		}                                                                        \
	} while (0)

extern struct obs_source_info test_sinewave;
extern struct obs_source_info test_random;
static volatile long saved_count;
static volatile long saving_count;

static void on_saved(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(cd);
	os_atomic_inc_long(&saved_count);
}

static void on_saving(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(cd);
	os_atomic_inc_long(&saving_count);
}

static void wait_count(volatile long *value, long target)
{
	uint64_t deadline = os_gettime_ns() + 15000000000ULL;
	while (os_atomic_load_long(value) < target && os_gettime_ns() < deadline)
		os_sleep_ms(10);
	CHECK(os_atomic_load_long(value) >= target);
}

static void load_module(const char *root, const char *name)
{
	struct dstr binary = {0};
	struct dstr data = {0};
	dstr_printf(&binary, "%s/obs-plugins/64bit/%s.dll", root, name);
	dstr_printf(&data, "%s/data/obs-plugins/%s", root, name);
	obs_module_t *module = NULL;
	CHECK(obs_open_module(&module, binary.array, data.array) == MODULE_SUCCESS);
	CHECK(obs_init_module(module));
	dstr_free(&binary);
	dstr_free(&data);
}

static void stop_output(obs_output_t *output)
{
	obs_output_stop(output);
	uint64_t deadline = os_gettime_ns() + 15000000000ULL;
	while (obs_output_active(output) && os_gettime_ns() < deadline)
		os_sleep_ms(10);
	CHECK(!obs_output_active(output));
}

static void save_replay(obs_output_t *output)
{
	calldata_t cd = {0};
	CHECK(proc_handler_call(obs_output_get_proc_handler(output), "save", &cd));
	calldata_free(&cd);
}

int main(int argc, char **argv)
{
	CHECK(argc == 6);
	const char *root = argv[1];
	const char *directory = argv[2];
	const char *encoder_id = argv[3];
	enum video_format format = strcmp(argv[4], "I444") == 0   ? VIDEO_FORMAT_I444
				   : strcmp(argv[4], "P010") == 0 ? VIDEO_FORMAT_P010
								  : VIDEO_FORMAT_NV12;
	int storage_mode = atoi(argv[5]);
	CHECK(os_mkdirs(directory) >= 0);
	CHECK(obs_startup("en-US", NULL, NULL));
	struct dstr path = {0};
	struct obs_audio_info audio = {.samples_per_sec = 48000, .speakers = SPEAKERS_STEREO};
	CHECK(obs_reset_audio(&audio));
	struct obs_video_info video = {
		.graphics_module = "libobs-d3d11",
		.fps_num = 60,
		.fps_den = 1,
		.base_width = 2560,
		.base_height = 1440,
		.output_width = 2560,
		.output_height = 1440,
		.output_format = format,
		.gpu_conversion = true,
		.colorspace = VIDEO_CS_709,
		.range = VIDEO_RANGE_PARTIAL,
		.scale_type = OBS_SCALE_BILINEAR,
	};
	CHECK(obs_reset_video(&video) == OBS_VIDEO_SUCCESS);
	load_module(root, "obs-ffmpeg");
	load_module(root, strstr(encoder_id, "nvenc") ? "obs-nvenc" : "obs-x264");
	obs_register_source(&test_sinewave);
	obs_register_source(&test_random);
	obs_post_load_modules();

	obs_source_t *random = obs_source_create("random", "Synthetic video", NULL, NULL);
	obs_source_t *sine = obs_source_create("test_sinewave", "Synthetic audio", NULL, NULL);
	CHECK(random && sine);
	obs_scene_t *scene = obs_scene_create("Synthetic scene");
	obs_sceneitem_t *item = obs_scene_add(scene, random);
	CHECK(item != NULL);
	struct vec2 scale = {.x = 128.0f, .y = 72.0f};
	obs_sceneitem_set_scale(item, &scale);
	obs_set_output_source(0, obs_scene_get_source(scene));
	obs_set_output_source(1, sine);

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", strstr(encoder_id, "nvenc") ? "CQP" : "CRF");
	obs_data_set_int(settings, "crf", 18);
	obs_data_set_int(settings, "cqp", 14);
	obs_data_set_int(settings, "keyint_sec", 1);
	obs_data_set_string(settings, "preset", strstr(encoder_id, "nvenc") ? "p6" : "ultrafast");
	obs_data_set_string(settings, "preset2", "p6");
	obs_data_set_string(settings, "multipass", "qres");
	obs_data_set_bool(settings, "lookahead", false);
	obs_data_set_int(settings, "bf", 0);
	obs_encoder_t *vencoder = obs_video_encoder_create(encoder_id, "Regression video", settings, NULL);
	obs_data_release(settings);
	CHECK(vencoder != NULL);
	obs_encoder_set_video(vencoder, obs_get_video());
	settings = obs_data_create();
	obs_data_set_int(settings, "bitrate", 160);
	obs_encoder_t *aencoder = obs_audio_encoder_create("ffmpeg_aac", "Regression audio", settings, 0, NULL);
	obs_data_release(settings);
	CHECK(aencoder != NULL);
	obs_encoder_set_audio(aencoder, obs_get_audio());

	settings = obs_data_create();
	obs_data_set_string(settings, "directory", directory);
	obs_data_set_string(settings, "format", "Replay-%CCYY-%MM-%DD-%hh-%mm-%ss");
	obs_data_set_string(settings, "extension", "mkv");
	obs_data_set_int(settings, "max_size_mb", 1);
	obs_data_set_int(settings, "max_time_sec", 2);
	obs_data_set_int(settings, "storage_mode", storage_mode);
	obs_output_t *replay = obs_output_create("replay_buffer", "Regression replay", settings, NULL);
	obs_data_release(settings);
	CHECK(replay != NULL);
	obs_output_set_video_encoder(replay, vencoder);
	obs_output_set_audio_encoder(replay, aencoder, 0);
	signal_handler_connect(obs_output_get_signal_handler(replay), "saved", on_saved, NULL);
	signal_handler_connect(obs_output_get_signal_handler(replay), "saving", on_saving, NULL);

	settings = obs_data_create();
	dstr_printf(&path, "%s/Recording.mkv", directory);
	obs_data_set_string(settings, "path", path.array);
	obs_output_t *recording = obs_output_create("ffmpeg_muxer", "Regression recording", settings, NULL);
	obs_data_release(settings);
	CHECK(recording != NULL);
	obs_output_set_video_encoder(recording, vencoder);
	obs_output_set_audio_encoder(recording, aencoder, 0);
	CHECK(obs_output_start(recording));
	CHECK(obs_output_start(replay));
	os_sleep_ms(5500);
	save_replay(replay);
	wait_count(&saved_count, 1);
	os_sleep_ms(2500);
	save_replay(replay);
	wait_count(&saving_count, 2);
	stop_output(replay);
	wait_count(&saved_count, 2);
	stop_output(recording);
	printf("PASS %s %s storage=%d: recording and 2 replay saves at 2560x1440/60\n", encoder_id, argv[4],
	       storage_mode);

	obs_output_release(replay);
	obs_output_release(recording);
	obs_encoder_release(vencoder);
	obs_encoder_release(aencoder);
	obs_set_output_source(0, NULL);
	obs_set_output_source(1, NULL);
	obs_scene_release(scene);
	obs_source_release(random);
	obs_source_release(sine);
	dstr_free(&path);
	obs_shutdown();
	return 0;
}
