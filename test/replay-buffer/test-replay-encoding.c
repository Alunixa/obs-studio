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
static volatile long failed_count;

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
	printf("SAVE STARTED %ld\n", os_atomic_load_long(&saving_count));
	fflush(stdout);
}

static void on_failed(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	CHECK(strlen(calldata_string(cd, "error")) > 0);
	os_atomic_inc_long(&failed_count);
}

static void check_single_cache(const char *directory)
{
	struct dstr path = {0};
	dstr_printf(&path, "%s/OBS-Replay-Cache/*", directory);
	os_glob_t *files = NULL;
	CHECK(os_glob(path.array, 0, &files) == 0);
	size_t count = 0;
	for (size_t i = 0; files && i < files->gl_pathc; i++) {
		dstr_printf(&path, "%s/cache.tmp", files->gl_pathv[i].path);
		count += os_file_exists(path.array) ? 1 : 0;
	}
	CHECK(count == 1);
	os_globfree(files);
	dstr_printf(&path, "%s/replay-buffer-*.tmp", directory);
	files = NULL;
	os_glob(path.array, 0, &files);
	CHECK(!files || files->gl_pathc == 0);
	os_globfree(files);
	dstr_free(&path);
}

static void wait_count(volatile long *value, long target)
{
	uint64_t deadline = os_gettime_ns() + 15000000000ULL;
	while (os_atomic_load_long(value) < target && os_gettime_ns() < deadline)
		os_sleep_ms(10);
	if (os_atomic_load_long(value) < target) {
		fprintf(stderr, "TIMEOUT target=%ld saved=%ld saving=%ld failed=%ld\n", target,
			os_atomic_load_long(&saved_count), os_atomic_load_long(&saving_count),
			os_atomic_load_long(&failed_count));
	}
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
	printf("SAVE REQUEST active=%d frames=%d\n", obs_output_active(output), obs_output_get_total_frames(output));
	fflush(stdout);
	calldata_t cd = {0};
	CHECK(proc_handler_call(obs_output_get_proc_handler(output), "save", &cd));
	calldata_free(&cd);
}

int main(int argc, char **argv)
{
	CHECK(argc == 6 || argc == 7);
	const char *root = argv[1];
	const char *directory = argv[2];
	const char *encoder_id = argv[3];
	enum video_format format = strcmp(argv[4], "I444") == 0   ? VIDEO_FORMAT_I444
				   : strcmp(argv[4], "P010") == 0 ? VIDEO_FORMAT_P010
								  : VIDEO_FORMAT_NV12;
	int storage_mode = atoi(argv[5]);
	bool window_test = argc == 7 && strcmp(argv[6], "window") == 0;
	CHECK(os_mkdirs(directory) >= 0);
	CHECK(obs_startup("en-US", NULL, NULL));
	struct dstr path = {0};
	struct obs_audio_info audio = {.samples_per_sec = 48000, .speakers = SPEAKERS_STEREO};
	CHECK(obs_reset_audio(&audio));
	struct obs_video_info video = {
		.graphics_module = "libobs-d3d11",
		.fps_num = 60,
		.fps_den = 1,
		.base_width = window_test ? 1280 : 2560,
		.base_height = window_test ? 720 : 1440,
		.output_width = window_test ? 1280 : 2560,
		.output_height = window_test ? 720 : 1440,
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
	struct vec2 scale = {.x = (float)video.base_width / 20.0f, .y = (float)video.base_height / 20.0f};
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
	obs_data_set_string(settings, "format", window_test ? "Replay" : "Replay-%CCYY-%MM-%DD-%hh-%mm-%ss");
	obs_data_set_string(settings, "extension", "mkv");
	obs_data_set_int(settings, "max_size_mb", window_test ? 64 : 1);
	obs_data_set_int(settings, "max_time_sec", window_test ? 5 : 2);
	obs_data_set_int(settings, "storage_mode", storage_mode);
	obs_output_t *replay = obs_output_create("replay_buffer", "Regression replay", settings, NULL);
	obs_data_release(settings);
	CHECK(replay != NULL);
	obs_output_set_video_encoder(replay, vencoder);
	obs_output_set_audio_encoder(replay, aencoder, 0);
	signal_handler_connect(obs_output_get_signal_handler(replay), "saved", on_saved, NULL);
	signal_handler_connect(obs_output_get_signal_handler(replay), "saving", on_saving, NULL);
	signal_handler_connect(obs_output_get_signal_handler(replay), "save_failed", on_failed, NULL);

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
	os_sleep_ms(window_test ? 6500 : 5500);
	if (storage_mode == 1)
		check_single_cache(directory);
	save_replay(replay);
	wait_count(&saved_count, 1);
	os_sleep_ms(window_test ? 250 : 2500);
	save_replay(replay);
	wait_count(&saving_count, 2);
	if (window_test) {
		wait_count(&saved_count, 2);
		CHECK(obs_output_active(replay));
		/* A regular file cannot be used as a directory. Fail only the
		 * export destination, not the already-open rolling cache. */
		struct dstr blocker = {0};
		dstr_printf(&blocker, "%s/output-path-blocker", directory);
		FILE *file = os_fopen(blocker.array, "wb");
		CHECK(file != NULL && fclose(file) == 0);
		settings = obs_output_get_settings(replay);
		obs_data_set_string(settings, "directory", blocker.array);
		obs_output_update(replay, settings);
		obs_data_release(settings);
		save_replay(replay);
		wait_count(&failed_count, 1);
		CHECK(obs_output_active(replay));
		CHECK(os_atomic_load_long(&saved_count) == 2);
		if (storage_mode == 1)
			check_single_cache(directory);
		settings = obs_output_get_settings(replay);
		obs_data_set_string(settings, "directory", directory);
		obs_output_update(replay, settings);
		obs_data_release(settings);
		os_sleep_ms(250);
		save_replay(replay);
		wait_count(&saved_count, 3);
		CHECK(obs_output_active(replay));
		CHECK(os_unlink(blocker.array) == 0);
		dstr_free(&blocker);
		puts("PASS continuous five-second window, 250ms save spacing, failed export and successful retry");
	}
	stop_output(replay);
	wait_count(&saved_count, window_test ? 3 : 2);
	stop_output(recording);
	printf("PASS %s %s storage=%d: recording and %ld replay saves at %ux%u/60\n", encoder_id, argv[4], storage_mode,
	       os_atomic_load_long(&saved_count), video.output_width, video.output_height);

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
