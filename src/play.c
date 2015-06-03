/**
 * \file play.c
 * \brief stream player
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/util.h>
#include <glc/common/state.h>

#include <glc/core/file.h>
#include <glc/core/pack.h>
#include <glc/core/rgb.h>
#include <glc/core/color.h>
#include <glc/core/info.h>
#include <glc/core/ycbcr.h>
#include <glc/core/scale.h>

#include <glc/export/img.h>
#include <glc/export/wav.h>
#include <glc/export/yuv4mpeg.h>

#include <glc/play/demux.h>

enum play_action {action_play, action_info, action_img, action_yuv4mpeg, action_wav, action_val};

struct play_s {
	glc_t glc;
	enum play_action action;

	glc_stream_info_t stream_info;
	char *info_name, *info_date;

	file_t file;
	const char *stream_file;

	double scale_factor;
	unsigned int scale_width, scale_height;

	size_t compressed_size, uncompressed_size;

	int override_color_correction;
	float brightness, contrast;
	float red_gamma, green_gamma, blue_gamma;

	int info_level;
	int interpolate;
	double fps;

	const char *export_filename_format;
	glc_stream_id_t export_video_id;
	glc_stream_id_t export_audio_id;
	int img_format;

	glc_utime_t silence_threshold;
	const char *alsa_playback_device;

	int log_level;
};

int show_info_value(struct play_s *play, const char *value);

int play_stream(struct play_s *play);
int stream_info(struct play_s *play);
int export_img(struct play_s *play);
int export_yuv4mpeg(struct play_s *play);
int export_wav(struct play_s *play);

int main(int argc, char *argv[])
{
	struct play_s play;
	play.action = action_play;
	const char *val_str = NULL;
	int opt;

	struct option long_options[] = {
		{"info",		1, NULL, 'i'},
		{"wav",			1, NULL, 'a'},
		{"bmp",			1, NULL, 'b'},
		{"png",			1, NULL, 'p'},
		{"yuv4mpeg",		1, NULL, 'y'},
		{"out",			1, NULL, 'o'},
		{"fps",			1, NULL, 'f'},
		{"resize",		1, NULL, 'r'},
		{"adjust",		1, NULL, 'g'},
		{"silence",		1, NULL, 'l'},
		{"alsa-device",		1, NULL, 'd'},
		{"streaming",		0, NULL, 't'},
		{"compressed",		1, NULL, 'c'},
		{"uncompressed",	1, NULL, 'u'},
		{"show",		1, NULL, 's'},
		{"verbosity",		1, NULL, 'v'},
		{"help",		0, NULL, 'h'},
		{"version",		0, NULL, 'V'},
		{0, 0, 0, 0}
	};

	play.fps = 0;

	play.silence_threshold = 200000; /* 0.2 sec accuracy */
	play.alsa_playback_device = "default";

	/* don't scale by default */
	play.scale_factor = 1;
	play.scale_width = play.scale_height = 0;

	/* default buffer size is 10MiB */
	play.compressed_size = 10 * 1024 * 1024;
	play.uncompressed_size = 10 * 1024 * 1024;

	/* log to stderr */
	play.log_level = 0;
	play.info_level = 1;

	/* default export settings */
	play.interpolate = 1;
	play.export_filename_format = NULL; /* user has to specify */
	play.img_format = IMG_BMP;

	/* global color correction */
	play.override_color_correction = 0;
	play.brightness = play.contrast = 0;
	play.red_gamma = 1.0;
	play.green_gamma = 1.0;
	play.blue_gamma = 1.0;

	while ((opt = getopt_long(argc, argv, "i:a:b:p:y:o:f:r:g:l:td:c:u:s:v:hV",
				  long_options, &optind)) != -1) {
		switch (opt) {
		case 'i':
			play.info_level = atoi(optarg);
			if (play.info_level < 1)
				goto usage;
			play.action = action_info;
			break;
		case 'a':
			play.export_audio_id = atoi(optarg);
			if (play.export_audio_id < 1)
				goto usage;
			play.action = action_wav;
			break;
		case 'p':
			play.img_format = IMG_PNG;
		case 'b':
			play.export_video_id = atoi(optarg);
			if (play.export_video_id < 1)
				goto usage;
			play.action = action_img;
			break;
		case 'y':
			play.export_video_id = atoi(optarg);
			if (play.export_video_id < 1)
				goto usage;
			play.action = action_yuv4mpeg;
			break;
		case 'f':
			play.fps = atof(optarg);
			if (play.fps <= 0)
				goto usage;
			break;
		case 'r':
			if (strstr(optarg, "x")) {
				sscanf(optarg, "%ux%u", &play.scale_width, &play.scale_height);
				if ((!play.scale_width) | (!play.scale_height))
					goto usage;
			} else {
				play.scale_factor = atof(optarg);
				if (play.scale_factor <= 0)
					goto usage;
			}
			break;
		case 'g':
			play.override_color_correction = 1;
			sscanf(optarg, "%f;%f;%f;%f;%f", &play.brightness, &play.contrast,
			       &play.red_gamma, &play.green_gamma, &play.blue_gamma);
			break;
		case 'l':
			/* glc_utime_t so always positive */
			play.silence_threshold = atof(optarg) * 1000000;
			break;
		case 'd':
			play.alsa_playback_device = optarg;
			break;
		case 'o':
			if (!strcmp(optarg, "-")) /** \todo fopen(1) ? */
				play.export_filename_format = "/dev/stdout";
			else
				play.export_filename_format = optarg;
			break;
		case 't':
			play.interpolate = 0;
			break;
		case 'c':
			play.compressed_size = atoi(optarg) * 1024 * 1024;
			if (play.compressed_size <= 0)
				goto usage;
			break;
		case 'u':
			play.uncompressed_size = atoi(optarg) * 1024 * 1024;
			if (play.uncompressed_size <= 0)
				goto usage;
			break;
		case 's':
			val_str = optarg;
			play.action = action_val;
			break;
		case 'v':
			play.log_level = atoi(optarg);
			if (play.log_level < 0)
				goto usage;
			break;
		case 'V':
			printf("glc version %s\n", glc_version());
			return EXIT_SUCCESS;
		case 'h':
		default:
			goto usage;
		}
	}

	/* stream file is mandatory */
	if (optind >= argc)
		goto usage;
	play.stream_file = argv[optind];

	/* same goes to output file */
	if (((play.action == action_img) |
	     (play.action == action_wav) |
	     (play.action == action_yuv4mpeg)) &&
	    (play.export_filename_format == NULL))
		goto usage;

	/* we do global initialization */
	glc_init(&play.glc);
	glc_log_set_level(&play.glc, play.log_level);
	glc_util_log_version(&play.glc);
	glc_state_init(&play.glc);

	/* open stream file */
	if (file_init(&play.file, &play.glc))
		return EXIT_FAILURE;
	if (file_open_source(play.file, play.stream_file))
		return EXIT_FAILURE;

	/* load information and check that the file is valid */
	if (file_read_info(play.file, &play.stream_info, &play.info_name, &play.info_date))
		return EXIT_FAILURE;

	/*
	 If the fps hasn't been specified read it from the
	 stream information.
	*/
	if (play.fps == 0)
		play.fps = play.stream_info.fps;

	switch (play.action) {
	case action_play:
		if (play_stream(&play))
			return EXIT_FAILURE;
		break;
	case action_wav:
		if (export_wav(&play))
			return EXIT_FAILURE;
		break;
	case action_yuv4mpeg:
		if (export_yuv4mpeg(&play))
			return EXIT_FAILURE;
		break;
	case action_img:
		if (export_img(&play))
			return EXIT_FAILURE;
		break;
	case action_info:
		if (stream_info(&play))
			return EXIT_FAILURE;
		break;
	case action_val:
		if (show_info_value(&play, val_str))
			return EXIT_FAILURE;
		break;
	}

	/* our cleanup */
	file_close_source(play.file);
	file_destroy(play.file);

	free(play.info_name);
	free(play.info_date);

	glc_state_destroy(&play.glc);
	glc_destroy(&play.glc);

	return EXIT_SUCCESS;

usage:
	printf("%s [file] [option]...\n", argv[0]);
	printf("  -i, --info=LEVEL         show stream information, LEVEL must be\n"
	       "                             greater than 0\n"
	       "  -a, --wav=NUM            save audio stream NUM in wav format\n"
	       "  -b, --bmp=NUM            save frames from stream NUM as bmp files\n"
	       "                             (use -o pic-%%010d.bmp f.ex.)\n"
	       "  -p, --png=NUM            save frames from stream NUM as png files\n"
	       "  -y, --yuv4mpeg=NUM       save video stream NUM in yuv4mpeg format\n"
	       "  -o, --out=FILE           write to FILE\n"
	       "  -f, --fps=FPS            save images or video at FPS\n"
	       "  -r, --resize=VAL         resize pictures with scale factor VAL or WxH\n"
	       "  -g, --color=ADJUST       adjust colors\n"
	       "                             format is brightness;contrast;red;green;blue\n"
	       "  -l, --silence=SECONDS    audio silence threshold in seconds\n"
	       "                             default threshold is 0.2\n"
	       "  -d, --alsa-device=DEV    alsa playback device name\n"
	       "                             default is 'default'\n"
	       "  -t, --streaming          streaming mode (eg. don't interpolate data)\n"
	       "  -c, --compressed=SIZE    compressed stream buffer size in MiB\n"
	       "                             default is 10 MiB\n"
	       "  -u, --uncompressed=SIZE  uncompressed stream buffer size in MiB\n"
	       "                             default is 10 MiB\n"
	       "  -s, --show=VAL           show stream summary value, possible values are:\n"
	       "                             all, signature, version, flags, fps,\n"
	       "                             pid, name, date\n"
	       "  -v, --verbosity=LEVEL    verbosity level\n"
	       "  -h, --help               show help\n");

	return EXIT_FAILURE;
}

int show_info_value(struct play_s *play, const char *value)
{
	if (!strcmp("all", value)) {
		printf("  signature   = 0x%08x\n", play->stream_info.signature);
		printf("  version     = 0x%02x\n", play->stream_info.version);
		printf("  flags       = %d\n", play->stream_info.flags);
		printf("  fps         = %f\n", play->stream_info.fps);
		printf("  pid         = %d\n", play->stream_info.pid);
		printf("  name        = %s\n", play->info_name);
		printf("  date        = %s\n", play->info_date);
	} else if (!strcmp("signature", value))
		printf("0x%08x\n", play->stream_info.signature);
	else if (!strcmp("version", value))
		printf("0x%02x\n", play->stream_info.version);
	else if (!strcmp("flags", value))
		printf("%d\n", play->stream_info.flags);
	else if (!strcmp("fps", value))
		printf("%f\n", play->stream_info.fps);
	else if (!strcmp("pid", value))
		printf("%d\n", play->stream_info.pid);
	else if (!strcmp("name", value))
		printf("%s\n", play->info_name);
	else if (!strcmp("date", value))
		printf("%s\n", play->info_date);
	else
		return ENOTSUP;
	return 0;
}

int play_stream(struct play_s *play)
{
	/*
	 Playback uses following pipeline:

	 file -(uncompressed)->     reads data from stream file
	 unpack -(uncompressed)->   decompresses lzo/quicklz packets
	 rgb -(rgb)->               does conversion to BGR
	 scale -(scale)->           does rescaling
	 color -(color)->           applies color correction
	 demux -(...)-> gl_play, alsa_play

	 Each filter, except demux and file, has glc_threads_hint(glc) worker
	 threads. Packet order in stream is preserved. Demux creates
	 separate buffer and _play handler for each video/audio stream.
	*/

	ps_bufferattr_t attr;
	ps_buffer_t uncompressed_buffer, compressed_buffer,
		    rgb_buffer, color_buffer, scale_buffer;
	demux_t demux;
	color_t color;
	scale_t scale;
	unpack_t unpack;
	rgb_t rgb;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/*
	 'compressed_buffer' buffer holds raw data from file and
	 has its own size.
	*/
	if ((ret = ps_bufferattr_setsize(&attr, play->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	/* rest use 'uncompressed_buffer' size */
	if ((ret = ps_bufferattr_setsize(&attr, play->uncompressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&uncompressed_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&color_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&rgb_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&scale_buffer, &attr)))
		goto err;

	/* no longer necessary */
	if ((ret = ps_bufferattr_destroy(&attr)))
		goto err;

	/* init filters */
	if ((ret = unpack_init(&unpack, &play->glc)))
		goto err;
	if ((ret = rgb_init(&rgb, &play->glc)))
		goto err;
	if ((ret = scale_init(&scale, &play->glc)))
		goto err;
	if (play->scale_width && play->scale_height)
		scale_set_size(scale, play->scale_width, play->scale_height);
	else
		scale_set_scale(scale, play->scale_factor);
	if ((ret = color_init(&color, &play->glc)))
		goto err;
	if (play->override_color_correction)
		color_override(color, play->brightness, play->contrast,
			       play->red_gamma, play->green_gamma, play->blue_gamma);
	if ((ret = demux_init(&demux, &play->glc)))
		goto err;
	demux_set_video_buffer_size(demux, play->uncompressed_size);
	demux_set_audio_buffer_size(demux, play->uncompressed_size / 10);
	demux_set_alsa_playback_device(demux, play->alsa_playback_device);

	/* construct a pipeline for playback */
	if ((ret = unpack_process_start(unpack, &compressed_buffer, &uncompressed_buffer)))
		goto err;
	if ((ret = rgb_process_start(rgb, &uncompressed_buffer, &rgb_buffer)))
		goto err;
	if ((ret = scale_process_start(scale, &rgb_buffer, &scale_buffer)))
		goto err;
	if ((ret = color_process_start(color, &scale_buffer, &color_buffer)))
		goto err;
	if ((ret = demux_process_start(demux, &color_buffer)))
		goto err;

	/* the pipeline is ready - lets give it some data */
	if ((ret = file_read(play->file, &compressed_buffer)))
		goto err;

	/* we've done our part - just wait for the threads */
	if ((ret = demux_process_wait(demux)))
		goto err; /* wait for demux, since when it quits, others should also */
	if ((ret = color_process_wait(color)))
		goto err;
	if ((ret = scale_process_wait(scale)))
		goto err;
	if ((ret = rgb_process_wait(rgb)))
		goto err;
	if ((ret = unpack_process_wait(unpack)))
		goto err;

	/* stream processed - clean up time */
	unpack_destroy(unpack);
	rgb_destroy(rgb);
	scale_destroy(scale);
	color_destroy(color);
	demux_destroy(demux);

	ps_buffer_destroy(&compressed_buffer);
	ps_buffer_destroy(&uncompressed_buffer);
	ps_buffer_destroy(&color_buffer);
	ps_buffer_destroy(&scale_buffer);
	ps_buffer_destroy(&rgb_buffer);

	return 0;
err:
	if (!ret) {
		fprintf(stderr, "playing stream failed: initializing filters failed\n");
		return EAGAIN;
	} else {
		fprintf(stderr, "playing stream failed: %s (%d)\n", strerror(ret), ret);
		return ret;
	}
}

int stream_info(struct play_s *play)
{
	/*
	 Info uses following pipeline:

	 file -(uncompressed_buffer)->     reads data from stream file
	 unpack -(uncompressed_buffer)->   decompresses lzo/quicklz packets
	 info -(rgb)->              shows stream information
	*/

	ps_bufferattr_t attr;
	ps_buffer_t uncompressed_buffer, compressed_buffer;
	info_t info;
	unpack_t unpack;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/* initialize buffers */
	if ((ret = ps_bufferattr_setsize(&attr, play->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_setsize(&attr, play->uncompressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&uncompressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_destroy(&attr)))
		goto err;

	/* and filters */
	if ((ret = unpack_init(&unpack, &play->glc)))
		goto err;
	if ((ret = info_init(&info, &play->glc)))
		goto err;
	info_set_level(info, play->info_level);

	/* run it */
	if ((ret = unpack_process_start(unpack, &compressed_buffer, &uncompressed_buffer)))
		goto err;
	if ((ret = info_process_start(info, &uncompressed_buffer)))
		goto err;
	if ((ret = file_read(play->file, &compressed_buffer)))
		goto err;

	/* wait for threads and do cleanup */
	if ((ret = info_process_wait(info)))
		goto err;
	if ((ret = unpack_process_wait(unpack)))
		goto err;

	unpack_destroy(unpack);
	info_destroy(info);

	ps_buffer_destroy(&compressed_buffer);
	ps_buffer_destroy(&uncompressed_buffer);

	return 0;
err:
	fprintf(stderr, "extracting stream information failed: %s (%d)\n", strerror(ret), ret);
	return ret;
}

int export_img(struct play_s *play)
{
	/*
	 Export img uses following pipeline:

	 file -(uncompressed_buffer)->     reads data from stream file
	 unpack -(uncompressed_buffer)->   decompresses lzo/quicklz packets
	 rgb -(rgb)->               does conversion to BGR
	 scale -(scale)->           does rescaling
	 color -(color)->           applies color correction
	 img                        writes separate image files for each frame
	*/

	ps_bufferattr_t attr;
	ps_buffer_t uncompressed_buffer, compressed_buffer,
		    rgb_buffer, color_buffer, scale_buffer;
	img_t img;
	color_t color;
	scale_t scale;
	unpack_t unpack;
	rgb_t rgb;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/* buffers */
	if ((ret = ps_bufferattr_setsize(&attr, play->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_setsize(&attr, play->uncompressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&uncompressed_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&color_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&rgb_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&scale_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_destroy(&attr)))
		goto err;

	/* filters */
	if ((ret = unpack_init(&unpack, &play->glc)))
		goto err;
	if ((ret = rgb_init(&rgb, &play->glc)))
		goto err;
	if ((ret = scale_init(&scale, &play->glc)))
		goto err;
	if (play->scale_width && play->scale_height)
		scale_set_size(scale, play->scale_width, play->scale_height);
	else
		scale_set_scale(scale, play->scale_factor);
	if ((ret = color_init(&color, &play->glc)))
		goto err;
	if (play->override_color_correction)
		color_override(color, play->brightness, play->contrast,
			       play->red_gamma, play->green_gamma, play->blue_gamma);
	if ((ret = img_init(&img, &play->glc)))
		goto err;
	img_set_filename(img, play->export_filename_format);
	img_set_stream_id(img, play->export_video_id);
	img_set_format(img, play->img_format);
	img_set_fps(img, play->fps);

	/* pipeline... */
	if ((ret = unpack_process_start(unpack, &compressed_buffer, &uncompressed_buffer)))
		goto err;
	if ((ret = rgb_process_start(rgb, &uncompressed_buffer, &rgb_buffer)))
		goto err;
	if ((ret = scale_process_start(scale, &rgb_buffer, &scale_buffer)))
		goto err;
	if ((ret = color_process_start(color, &scale_buffer, &color_buffer)))
		goto err;
	if ((ret = img_process_start(img, &color_buffer)))
		goto err;

	/* ok, read the file */
	if ((ret = file_read(play->file, &compressed_buffer)))
		goto err;

	/* wait 'till its done and clean up the mess... */
	if ((ret = img_process_wait(img)))
		goto err;
	if ((ret = color_process_wait(color)))
		goto err;
	if ((ret = scale_process_wait(scale)))
		goto err;
	if ((ret = rgb_process_wait(rgb)))
		goto err;
	if ((ret = unpack_process_wait(unpack)))
		goto err;

	unpack_destroy(unpack);
	rgb_destroy(rgb);
	scale_destroy(scale);
	color_destroy(color);
	img_destroy(img);

	ps_buffer_destroy(&compressed_buffer);
	ps_buffer_destroy(&uncompressed_buffer);
	ps_buffer_destroy(&color_buffer);
	ps_buffer_destroy(&scale_buffer);
	ps_buffer_destroy(&rgb_buffer);

	return 0;
err:
	fprintf(stderr, "exporting images failed: %s (%d)\n", strerror(ret), ret);
	return ret;
}

int export_yuv4mpeg(struct play_s *play)
{
	/*
	 Export yuv4mpeg uses following pipeline:

	 file -(uncompressed_buffer)->     reads data from stream file
	 unpack -(uncompressed_buffer)->   decompresses lzo/quicklz packets
	 scale -(scale)->           does rescaling
	 color -(color)->           applies color correction
	 ycbcr -(ycbcr)->           does conversion to Y'CbCr (if necessary)
	 yuv4mpeg                   writes yuv4mpeg stream
	*/

	ps_bufferattr_t attr;
	ps_buffer_t uncompressed_buffer, compressed_buffer,
		    ycbcr_buffer, color_buffer, scale_buffer;
	yuv4mpeg_t yuv4mpeg;
	ycbcr_t ycbcr;
	scale_t scale;
	unpack_t unpack;
	color_t color;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/* buffers */
	if ((ret = ps_bufferattr_setsize(&attr, play->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_setsize(&attr, play->uncompressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&uncompressed_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&color_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&ycbcr_buffer, &attr)))
		goto err;
	if ((ret = ps_buffer_init(&scale_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_destroy(&attr)))
		goto err;

	/* initialize filters */
	if ((ret = unpack_init(&unpack, &play->glc)))
		goto err;
	if ((ret = ycbcr_init(&ycbcr, &play->glc)))
		goto err;
	if ((ret = scale_init(&scale, &play->glc)))
		goto err;
	if (play->scale_width && play->scale_height)
		scale_set_size(scale, play->scale_width, play->scale_height);
	else
		scale_set_scale(scale, play->scale_factor);
	if ((ret = color_init(&color, &play->glc)))
		goto err;
	if (play->override_color_correction)
		color_override(color, play->brightness, play->contrast,
			       play->red_gamma, play->green_gamma, play->blue_gamma);
	if ((ret = yuv4mpeg_init(&yuv4mpeg, &play->glc)))
		goto err;
	yuv4mpeg_set_fps(yuv4mpeg, play->fps);
	yuv4mpeg_set_stream_id(yuv4mpeg, play->export_video_id);
	yuv4mpeg_set_interpolation(yuv4mpeg, play->interpolate);
	yuv4mpeg_set_filename(yuv4mpeg, play->export_filename_format);

	/* construct the pipeline */
	if ((ret = unpack_process_start(unpack, &compressed_buffer, &uncompressed_buffer)))
		goto err;
	if ((ret = scale_process_start(scale, &uncompressed_buffer, &scale_buffer)))
		goto err;
	if ((ret = color_process_start(color, &scale_buffer, &color_buffer)))
		goto err;
	if ((ret = ycbcr_process_start(ycbcr, &color_buffer, &ycbcr_buffer)))
		goto err;
	if ((ret = yuv4mpeg_process_start(yuv4mpeg, &ycbcr_buffer)))
		goto err;

	/* feed it with data */
	if ((ret = file_read(play->file, &compressed_buffer)))
		goto err;

	/* threads will do the dirty work... */
	if ((ret = yuv4mpeg_process_wait(yuv4mpeg)))
		goto err;
	if ((ret = color_process_wait(color)))
		goto err;
	if ((ret = scale_process_wait(scale)))
		goto err;
	if ((ret = ycbcr_process_wait(ycbcr)))
		goto err;
	if ((ret = unpack_process_wait(unpack)))
		goto err;

	unpack_destroy(unpack);
	ycbcr_destroy(ycbcr);
	scale_destroy(scale);
	color_destroy(color);
	yuv4mpeg_destroy(yuv4mpeg);

	ps_buffer_destroy(&compressed_buffer);
	ps_buffer_destroy(&uncompressed_buffer);
	ps_buffer_destroy(&color_buffer);
	ps_buffer_destroy(&scale_buffer);
	ps_buffer_destroy(&ycbcr_buffer);

	return 0;
err:
	fprintf(stderr, "exporting yuv4mpeg failed: %s (%d)\n", strerror(ret), ret);
	return ret;
}

int export_wav(struct play_s *play)
{
	/*
	 Export wav uses following pipeline:

	 file -(uncompressed_buffer)->     reads data from stream file
	 unpack -(uncompressed_buffer)->   decompresses lzo/quicklz packets
	 wav -(rgb)->               write audio to file in wav format
	*/

	ps_bufferattr_t attr;
	ps_buffer_t uncompressed_buffer, compressed_buffer;
	wav_t wav;
	unpack_t unpack;
	int ret = 0;

	if ((ret = ps_bufferattr_init(&attr)))
		goto err;

	/* buffers */
	if ((ret = ps_bufferattr_setsize(&attr, play->compressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&compressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_setsize(&attr, play->uncompressed_size)))
		goto err;
	if ((ret = ps_buffer_init(&uncompressed_buffer, &attr)))
		goto err;

	if ((ret = ps_bufferattr_destroy(&attr)))
		goto err;

	/* init filters */
	if ((ret = unpack_init(&unpack, &play->glc)))
		goto err;
	if ((ret = wav_init(&wav, &play->glc)))
		goto err;
	wav_set_interpolation(wav, play->interpolate);
	wav_set_filename(wav, play->export_filename_format);
	wav_set_stream_id(wav, play->export_audio_id);
	wav_set_silence_threshold(wav, play->silence_threshold);

	/* start the threads */
	if ((ret = unpack_process_start(unpack, &compressed_buffer, &uncompressed_buffer)))
		goto err;
	if ((ret = wav_process_start(wav, &uncompressed_buffer)))
		goto err;
	if ((ret = file_read(play->file, &compressed_buffer)))
		goto err;

	/* wait and clean up */
	if ((ret = wav_process_wait(wav)))
		goto err;
	if ((ret = unpack_process_wait(unpack)))
		goto err;

	unpack_destroy(unpack);
	wav_destroy(wav);

	ps_buffer_destroy(&compressed_buffer);
	ps_buffer_destroy(&uncompressed_buffer);

	return 0;
err:
	if (!ret) {
		fprintf(stderr, "exporting wav failed: initializing filters failed\n");
		return EAGAIN;
	} else {
		fprintf(stderr, "exporting wav failed: %s (%d)\n", strerror(ret), ret);
		return ret;
	}
}
