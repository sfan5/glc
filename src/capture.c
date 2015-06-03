/**
 * \file capture.c
 * \brief capture utility
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/* for setenv() */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>

struct glc_opt_s {
	char short_name;
	const char *name;
	const char *env;
	const char *val;
};

int env_append(const char *env, const char *val, char separator);
int parse_arg(struct glc_opt_s *options, int argc, char *argv[], int *optind);
int set_opt(struct glc_opt_s *option, const char *arg);
struct glc_opt_s *find_opt_long(struct glc_opt_s *options, const char *name, size_t len);
struct glc_opt_s *find_opt_short(struct glc_opt_s *options, const char short_name);

int main(int argc, char *argv[])
{
	int optind;
	int ret = 0;

	char *program = NULL;
	char **program_args = NULL;
	const char *library = "libglc-hook.so";
	const char *core_library = "libglc-core.so";
	const char *(*glc_version)();

	struct glc_opt_s options[] = {
		{'o', "out",			"GLC_FILE",			NULL},
		{'f', "fps",			"GLC_FPS",			NULL},
		{'r', "resize",			"GLC_SCALE",			NULL},
		{'c', "crop",			"GLC_CROP",			NULL},
		{'a', "record-audio",		"GLC_AUDIO_RECORD",		NULL},
		{'s', "start",			"GLC_START",			 "1"},
		{'e', "colorspace",		"GLC_COLORSPACE",		NULL},
		{'k', "hotkey",			"GLC_HOTKEY",			NULL},
		{ 0 , "reload",			"GLC_RELOAD_HOTKEY",		NULL},
		{'n', "lock-fps",		"GLC_LOCK_FPS",			 "1"},
		{ 0 , "pbo",			"GLC_TRY_PBO",			 "1"},
		{'z', "compression",		"GLC_COMPRESS",			NULL},
		{ 0 , "sync",			"GLC_SYNC",			 "1"},
		{ 0 , "byte-aligned",		"GLC_CAPTURE_DWORD_ALIGNED",	 "0"},
		{'i', "draw-indicator",		"GLC_INDICATOR",		 "1"},
		{'v', "log",			"GLC_LOG",			NULL},
		{'l', "log-file",		"GLC_LOG_FILE",			NULL},
		{ 0 , "audio-skip",		"GLC_AUDIO_SKIP",		 "1"},
		{ 0 , "disable-audio",		"GLC_AUDIO",			 "0"},
		{ 0 , "sighandler",		"GLC_SIGHANDLER",		 "1"},
		{'g', "glfinish",		"GLC_CAPTURE_GLFINISH",		 "1"},
		{'j', "force-sdl-alsa-drv",	"SDL_AUDIODRIVER",	      "alsa"},
		{'b', "capture",		"GLC_CAPTURE",			NULL},
		{ 0 , "compressed",		"GLC_COMPRESSED_BUFFER_SIZE",	NULL},
		{ 0 , "uncompressed",		"GLC_UNCOMPRESSED_BUFFER_SIZE",	NULL},
		{ 0 , "unscaled",		"GLC_UNSCALED_BUFFER_SIZE",	NULL},
		{ 0 , NULL,			NULL,				NULL}
	};

	/* check that libglc-core.so can be loaded */
	void *handle = dlopen(core_library, RTLD_LAZY);
	if (handle == NULL) {
		fprintf(stderr, "Can't find glc libraries\n");
		return EXIT_FAILURE;
	}
	glc_version = (const char*(*)()) dlsym(handle, "glc_version");
	if (glc_version == NULL) {
		fprintf(stderr, "Invalid glc libraries\n");
		return EXIT_FAILURE;
	}

	/* Always set GLC_FILE in order to route it into $PWD.
	 * If -o is set, it will just silently override default one.
	 */
	/* \note Assumes that GLC_FILE exists at [0]. */
	set_opt(&options[0], "%app%-%pid%-%capture%.glc");

	/* parse options until we encounter first invalid option or non-option argument */
	for (optind = 1; optind < argc;) {
		/* test if this is --version */
		if ((!strcmp("--version", argv[optind])) | (!strcmp("-V", argv[optind]))) {
			printf("glc version %s\n", glc_version());
			dlclose(handle);
			return EXIT_SUCCESS;
		}

		if ((ret = parse_arg(options, argc, argv, &optind))) {
			if (ret == EINVAL)
				goto usage;
			else
				break;
		}
	}

	dlclose(handle);

	/* add libglc-hook.so library to the LD_PRELOAD environment variable */
	env_append("LD_PRELOAD", library, ':');

	if (optind >= argc)
		goto usage;

	program = argv[optind];
	program_args = &argv[optind]; /* first argument is always program name */

	/*
	fprintf(stderr, "%s", program);
	while (optind < argc)
		fprintf(stderr, " %s", argv[optind++]);
	fprintf(stderr, "\n");
	*/

	/* and finally: execute it... */
	if ((ret = execvp(program, program_args))) {
		fprintf(stderr, "can't execute \"%s", program);
		optind++;
		while (optind < argc)
			fprintf(stderr, " %s", argv[optind++]);
		fprintf(stderr, "\"\n");
		/*fprintf(stderr, "%s (%d)\n", strerror(ret), ret);*/
		return ret;
	}

	return EXIT_SUCCESS;
usage:
	printf("%s [capture option]... [application] [application argument]...\n", argv[0]);
	printf("  -o, --out=FILE             write to FILE\n"
	       "                               following tags are available:\n"
	       "                                 %%app%%:     application name\n"
	       "                                 %%pid%%:     process ID\n"
	       "                                 %%capture%%: counter\n"
	       "                                 %%year%%:    4-digit year\n"
	       "                                 %%month%%:   2-digit month\n"
	       "                                 %%day%%:     2-digit day\n"
	       "                                 %%hour%%:    2-digit hour\n"
	       "                                 %%min%%:     2-digit minute\n"
	       "                                 %%sec%%:     2-digit second\n"
	       "                               default value is %%app%%-%%pid%%-%%capture%%.glc\n"
	       "  -f, --fps=FPS              capture at FPS, default value is 30\n"
	       "  -r, --resize=FACTOR        resize pictures with scale factor FACTOR\n"
	       "  -c, --crop=WxH+X+Y         capture only [width]x[height][+[x][+[y]]]\n"
	       "  -a, --record-audio=CONFIG  record specified alsa devices\n"
	       "                               format is device,rate,channels;device2...\n"
	       "  -s, --start                start capturing immediately\n"
	       "  -e, --colorspace=CSP       keep as 'bgr' or convert to '420jpeg'\n"
	       "                               default value is '420jpeg'\n"
	       "  -k, --hotkey=HOTKEY        capture hotkey, <Ctrl> and <Shift> modifiers are\n"
	       "                               supported, default hotkey is '<Shift>F8'\n"
	       "      --reload=HOTKEY        reload hotkey, switches to next capture file\n"
	       "                               default reload key is '<Shift>F9'\n"
	       "  -n, --lock-fps             lock fps when capturing\n"
	       "      --pbo                  use GL_ARB_pixel_buffer_object if available\n"
	       "  -z, --compression=METHOD   compress stream using METHOD\n"
	       "                               'none', 'quicklz', 'lzo' and 'lzjb' are supported\n"
	       "                               'quicklz' is used by default\n"
	       "      --sync                 force synchronized write mode\n"
	       "      --byte-aligned         use GL_PACK_ALIGNMENT 1 instead of 8\n"
	       "  -i, --draw-indicator       draw indicator when capturing\n"
	       "                               indicator does not work with -b 'front'\n"
	       "  -v, --log=LEVEL            log >=LEVEL messages\n"
	       "                               0: errors\n"
	       "                               1: warnings\n"
	       "                               2: performance information\n"
	       "                               3: information\n"
	       "                               4: debug\n"
	       "  -l, --log-file=FILE        write log to FILE, pid-%%d.log by default\n"
	       "      --audio-skip           skip audio packets if buffer is full\n"
	       "                               or capture thread is busy\n"
	       "      --disable-audio        don't capture audio\n"
	       "      --sighandler           use custom signal handler\n"
	       "  -g, --glfinish             capture at glFinish()\n"
	       "  -j, --force-sdl-alsa-drv   force SDL to use ALSA audio driver\n"
	       "  -b, --capture=BUFFER       capture 'front' or 'back' buffer\n"
	       "                               default is 'front'\n"
	       "      --compressed=SIZE      compressed stream buffer size in MiB\n"
	       "                               default is 50 MiB\n"
	       "      --uncompressed=SIZE    uncompressed stream buffer size in MiB\n"
	       "                               default is 25 MiB\n"
	       "      --unscaled=SIZE        unscaled picture stream buffer size in MiB,\n"
	       "                               default is 25 MiB\n"
	       "  -V, --version              print glc version and exit\n"
	       "  -h, --help                 show this help\n");
	return EXIT_FAILURE;
}

int env_append(const char *env, const char *val, char separator)
{
	size_t env_len;
	const char *old_env;
	char *new_env;

	old_env = getenv(env);

	if (old_env != NULL) {
		env_len = strlen(old_env) + strlen(val) + 2;
		new_env = malloc(env_len);
		
		memcpy(new_env, old_env, strlen(old_env));
		memcpy(&new_env[strlen(old_env) + 1], val, strlen(val));
		
		new_env[strlen(old_env)] = separator;
		new_env[env_len - 1] = '\0';
	} else {
		env_len = strlen(val) + 1;
		new_env = malloc(env_len);

		memcpy(new_env, val, env_len - 1);
		new_env[env_len - 1] = '\0';
	}

	setenv(env, new_env, 1);
	free(new_env);

	return 0;
}

int parse_arg(struct glc_opt_s *options, int argc, char *argv[], int *optind)
{
	int ret;
	const char *arg;
	const char *argopt;
	struct glc_opt_s *opt;
	size_t arg_len;

	if (*optind >= argc)
		return EINVAL;

	arg = argv[*optind];
	arg_len = strlen(arg);

	if (arg_len < 2)
		return 1;

	if (*arg != '-')
		return 1;
	arg++; /* skip - */

	if (arg_len > 2) {
		if (*arg == '-') {
			arg++; /* skip - */

			/* handle --arg=val */
			if ((argopt = strstr(arg, "=")) != NULL) {
				if (!(opt = find_opt_long(options, arg, argopt - arg)))
					return EINVAL;
				if (!(ret = set_opt(opt, ++argopt)))
					(*optind)++;
				return ret;
			}

			if (!(opt = find_opt_long(options, arg, strlen(arg))))
				return EINVAL;

			/* does argument expect value */
			if (opt->val == NULL) {
				(*optind)++;
				if ((*optind) >= argc)
					return EINVAL;
				argopt = argv[*optind];
			} else
				argopt = NULL;

			if (!(ret = set_opt(opt, argopt)))
				(*optind)++;
			return ret;
		}
	}

	/* parse -flags [val]*/
	while (*arg != '\0') {
		if (!(opt = find_opt_short(options, *arg)))
			return EINVAL;

		if (opt->val == NULL) {
			/* flag in the middle of some list can't have an argument */
			if (arg[1] != '\0')
				return EINVAL;

			(*optind)++;
			if ((*optind) >= argc)
				return EINVAL;

			argopt = argv[*optind];
			if (!(ret = set_opt(opt, argopt)))
				(*optind)++;
			return ret;
		}

		/* doesn't expect an argument */
		if ((ret = set_opt(opt, NULL)))
			return ret;

		arg++;
	}
	(*optind)++;

	return 0;
}

int set_opt(struct glc_opt_s *option, const char *arg)
{
	/*
	fprintf(stderr, "set_opt({%c, %s, %s, %s}, %s)\n", option->short_name,
		option->name, option->env, option->val, arg);
	*/

	/*
	 Append current working directory to -o and -l
	 if they don't start with /. Otherwise running capture
	 against script can cause glc to write those files into different
	 directory.
	 */
	char *fullpath;
	size_t len, pos;
	if ((option->short_name == 'o') || (option->short_name == 'l')) {
		if (arg == NULL)
			return EINVAL; /* no segfaults, thanks */

		if (arg[0] != '/') {
			fullpath = malloc(1024);
			fullpath[0] = '\0'; /* just to make sure */

			getcwd(fullpath, 1024);

			pos = strlen(fullpath);
			len = strlen(arg);
			if (len + pos + 1 > 1024)
				len = 1023 - pos;

			fullpath[pos++] = '/';
			memcpy(&fullpath[pos], arg, len);
			fullpath[pos + len] = '\0';

			setenv(option->env, fullpath, 1);

			free(fullpath);

			return 0;
		}
	}

	/* otherwise just normal argument handling */
	if (option->val == NULL) {
		if (arg == NULL)
			return EINVAL;
		setenv(option->env, arg, 1);
	} else {
		if (arg != NULL)
			return EINVAL;
		setenv(option->env, option->val, 1);
	}

	return 0;
}

struct glc_opt_s *find_opt_long(struct glc_opt_s *options, const char *name, size_t len)
{
	int opt = 0;

	while (options[opt].name != NULL) {
		if (!strncmp(options[opt].name, name, len))
			return &options[opt];
		opt++;
	}

	return NULL;
}

struct glc_opt_s *find_opt_short(struct glc_opt_s *options, const char short_name)
{
	int opt = 0;

	if (short_name == '\0')
		return NULL;

	while (options[opt].name != NULL) {
		if (options[opt].short_name == short_name)
			return &options[opt];
		opt++;
	}

	return NULL;
}
