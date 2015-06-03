/**
 * \file glc/core/color.c
 * \brief color correction
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup color
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <packetstream.h>
#include <errno.h>
#include <math.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/thread.h>
#include <glc/common/util.h>

#include "color.h"

#define LOOKUP_BITS 8
#define YCBCR_LOOKUP_POS(Y, Cb, Cr) \
	(((((Y)  >> (8 - LOOKUP_BITS)) << (LOOKUP_BITS * 2)) + \
	  (((Cb) >> (8 - LOOKUP_BITS)) << LOOKUP_BITS) + \
	  ( (Cr) >> (8 - LOOKUP_BITS))) * 3)

#define COLOR_RUNNING     0x1
#define COLOR_OVERRIDE    0x2

struct color_video_stream_s;

typedef void (*color_proc)(color_t color, struct color_video_stream_s *video,
			   unsigned char *from, unsigned char *to);

struct color_video_stream_s {
	glc_stream_id_t id;
	glc_video_format_t format;
	unsigned int w, h;

	unsigned int bpp, row;

	float brightness, contrast;
	float red_gamma, green_gamma, blue_gamma;

	unsigned char *lookup_table;
	color_proc proc;

	pthread_rwlock_t update;
	struct color_video_stream_s *next;
};

struct color_s {
	glc_t *glc;
	glc_flags_t flags;
	glc_thread_t thread;

	struct color_video_stream_s *video;

	float brightness, contrast;
	float red_gamma, green_gamma, blue_gamma;
};

int color_read_callback(glc_thread_state_t *state);
int color_write_callback(glc_thread_state_t *state);
void color_finish_callback(void *ptr, int err);

void color_get_video_stream(color_t color, glc_stream_id_t id,
		   struct color_video_stream_s **video);

int color_video_format_msg(color_t color, glc_video_format_message_t *msg);
int color_color_msg(color_t color, glc_color_message_t *msg);

int color_generate_ycbcr_lookup_table(color_t color,
				      struct color_video_stream_s *video);
int color_generate_rgb_lookup_table(color_t color,
				    struct color_video_stream_s *video);

void color_ycbcr(color_t color, struct color_video_stream_s *video,
		 unsigned char *from, unsigned char *to);
void color_bgr(color_t color, struct color_video_stream_s *video,
	       unsigned char *from, unsigned char *to);

/* unfortunately over- and underflows will occur */
static __inline__ unsigned char color_clamp(int val)
{
	if (val > 255)
		return 255;
	else if (val < 0)
		return 0;
	return val;
}

int color_init(color_t *color, glc_t *glc)
{
	*color = malloc(sizeof(struct color_s));
	memset(*color, 0, sizeof(struct color_s));

	(*color)->glc = glc;

	(*color)->thread.flags = GLC_THREAD_READ | GLC_THREAD_WRITE;
	(*color)->thread.read_callback = &color_read_callback;
	(*color)->thread.write_callback = &color_write_callback;
	(*color)->thread.finish_callback = &color_finish_callback;
	(*color)->thread.ptr = *color;
	(*color)->thread.threads = glc_threads_hint(glc);

	return 0;
}

int color_destroy(color_t color)
{
	free(color);
	return 0;
}

int color_process_start(color_t color, ps_buffer_t *from, ps_buffer_t *to)
{
	int ret;
	if (color->flags & COLOR_RUNNING)
		return EAGAIN;

	if ((ret = glc_thread_create(color->glc, &color->thread, from, to)))
		return ret;
	color->flags |= COLOR_RUNNING;

	return 0;
}

int color_process_wait(color_t color)
{
	if (!(color->flags & COLOR_RUNNING))
		return EAGAIN;

	glc_thread_wait(&color->thread);
	color->flags &= ~COLOR_RUNNING;

	return 0;
}

int color_override(color_t color, float brightness, float contrast,
			    float red, float green, float blue)
{
	color->brightness = brightness;
	color->contrast = contrast;
	color->red_gamma = red;
	color->green_gamma = green;
	color->blue_gamma = blue;

	color->flags |= COLOR_OVERRIDE;
	return 0;
}

int color_override_clear(color_t color)
{
	color->flags &= ~COLOR_OVERRIDE;
	return 0;
}

void color_finish_callback(void *ptr, int err)
{
	color_t color = (color_t) ptr;
	struct color_video_stream_s *del;

	if (err)
		glc_log(color->glc, GLC_ERROR, "color", "%s (%d)", strerror(err), err);

	while (color->video != NULL) {
		del = color->video;
		color->video = color->video->next;

		pthread_rwlock_destroy(&del->update);
		if (del->lookup_table)
			free(del->lookup_table);
		free(del);
	}
}

int color_read_callback(glc_thread_state_t *state)
{
	color_t color = (color_t) state->ptr;
	struct color_video_stream_s *video;
	glc_video_frame_header_t *pic_hdr;

	if (state->header.type == GLC_MESSAGE_COLOR) {
		color_color_msg(color, (glc_color_message_t *) state->read_data);

		/* color correction should be done */
		state->flags |= GLC_THREAD_STATE_SKIP_WRITE;
		return 0;
	}

	if (state->header.type == GLC_MESSAGE_VIDEO_FORMAT)
		color_video_format_msg(color, (glc_video_format_message_t *) state->read_data);

	if (state->header.type == GLC_MESSAGE_VIDEO_FRAME) {
		pic_hdr = (glc_video_frame_header_t *) state->read_data;
		color_get_video_stream(color, pic_hdr->id, &video);
		state->threadptr = video;

		pthread_rwlock_rdlock(&video->update);

		if (video->proc == NULL) {
			state->flags |= GLC_THREAD_COPY;
			pthread_rwlock_unlock(&video->update);
		}
	} else
		state->flags |= GLC_THREAD_COPY;

	return 0;
}

int color_write_callback(glc_thread_state_t *state)
{
	struct color_video_stream_s *video = state->threadptr;

	memcpy(state->write_data, state->read_data, sizeof(glc_video_frame_header_t));
	video->proc(state->ptr, video,
		  (unsigned char *) &state->read_data[sizeof(glc_video_frame_header_t)],
		  (unsigned char *) &state->write_data[sizeof(glc_video_frame_header_t)]);

	pthread_rwlock_unlock(&video->update);
	return 0;
}

void color_get_video_stream(color_t color, glc_stream_id_t id,
		   struct color_video_stream_s **video)
{
	/* this function is called from read callback so it is never
	   called in parallel */
	*video = color->video;

	while (*video != NULL) {
		if ((*video)->id == id)
			break;
		*video = (*video)->next;
	}

	if (*video == NULL) {
		*video = malloc(sizeof(struct color_video_stream_s));
		memset(*video, 0, sizeof(struct color_video_stream_s));

		(*video)->next = color->video;
		color->video = *video;
		(*video)->id = id;
		pthread_rwlock_init(&(*video)->update, NULL);
	}
}

int color_video_format_msg(color_t color, glc_video_format_message_t *msg)
{
	struct color_video_stream_s *video;
	glc_video_format_t old_format;

	color_get_video_stream(color, msg->id, &video);
	pthread_rwlock_wrlock(&video->update);

	old_format = video->format;
	video->format = msg->format;
	video->w = msg->width;
	video->h = msg->height;

	if ((video->format == GLC_VIDEO_BGR) |
	    (video->format == GLC_VIDEO_BGRA)) {
		if (video->format == GLC_VIDEO_BGRA)
			video->bpp = 4;
		else
			video->bpp = 3;

		video->row = video->bpp * video->w;

		if ((msg->flags & GLC_VIDEO_DWORD_ALIGNED) &&
		    (video->row % 8 != 0))
			video->row += 8 - video->row % 8;
	}

	if (color->flags & COLOR_OVERRIDE) {
		video->brightness = color->brightness;
		video->contrast = color->contrast;
		video->red_gamma = color->red_gamma;
		video->green_gamma = color->green_gamma;
		video->blue_gamma = color->blue_gamma;

		glc_log(color->glc, GLC_INFORMATION, "color",
			 "using global color correction for video %d", msg->id);
		glc_log(color->glc, GLC_INFORMATION, "color",
			 "video stream %d: brightness=%f, contrast=%f, red=%f, green=%f, blue=%f",
			 msg->id, video->brightness, video->contrast,
			 video->red_gamma, video->green_gamma, video->blue_gamma);

		if ((video->brightness == 0) &&
		    (video->contrast == 0) &&
		    (video->red_gamma == 1) &&
		    (video->green_gamma == 1) &&
		    (video->blue_gamma == 1)) {
			glc_log(color->glc, GLC_INFORMATION, "color", "skipping color correction");
			video->proc = NULL;
		} else if (video->format == GLC_VIDEO_YCBCR_420JPEG) {
			color_generate_ycbcr_lookup_table(color, video);
			video->proc = &color_ycbcr;
		} else if ((video->format == GLC_VIDEO_BGR) | (video->format == GLC_VIDEO_BGRA)) {
			color_generate_rgb_lookup_table(color, video);
			video->proc = &color_bgr;
		} else {
			/* set proc NULL -> no conversion done */
			glc_log(color->glc, GLC_WARNING, "color", "unsupported video %d", msg->id);
			video->proc = NULL;
		}
	} else if (((old_format == GLC_VIDEO_BGR) |
		    (old_format == GLC_VIDEO_BGRA)) &&
		   (msg->format == GLC_VIDEO_YCBCR_420JPEG)) {
		glc_log(color->glc, GLC_WARNING, "color",
			 "colorspace switched from RGB to Y'CbCr, recalculating lookup table");
		color_generate_ycbcr_lookup_table(color, video);
		video->proc = &color_ycbcr;
	} else if (((msg->format == GLC_VIDEO_BGR) |
		    (msg->format == GLC_VIDEO_BGRA)) &&
		   (old_format == GLC_VIDEO_YCBCR_420JPEG)) {
		glc_log(color->glc, GLC_WARNING, "color",
			 "colorspace switched from Y'CbCr to RGB, recalculating lookup table");
		color_generate_rgb_lookup_table(color, video);
		video->proc = &color_bgr;
	}

	pthread_rwlock_unlock(&video->update);
	return 0;
}

int color_color_msg(color_t color, glc_color_message_t *msg)
{
	struct color_video_stream_s *video;

	if (color->flags & COLOR_OVERRIDE)
		return 0; /* ignore */

	color_get_video_stream(color, msg->id, &video);
	pthread_rwlock_wrlock(&video->update);

	video->brightness = msg->brightness;
	video->contrast = msg->contrast;
	video->red_gamma = msg->red;
	video->green_gamma = msg->green;
	video->blue_gamma = msg->blue;

	glc_log(color->glc, GLC_INFORMATION, "color",
		 "video stream %d: brightness=%f, contrast=%f, red=%f, green=%f, blue=%f",
		 msg->id, video->brightness, video->contrast,
		 video->red_gamma, video->green_gamma, video->blue_gamma);

	if ((video->brightness == 0) &&
	    (video->contrast == 0) &&
	    (video->red_gamma == 1) &&
	    (video->green_gamma == 1) &&
	    (video->blue_gamma == 1)) {
		glc_log(color->glc, GLC_INFORMATION, "color", "skipping color correction");
		video->proc = NULL;
	} else if (video->format == GLC_VIDEO_YCBCR_420JPEG) {
		color_generate_ycbcr_lookup_table(color, video);
		video->proc = &color_ycbcr;
	} else if ((video->format == GLC_VIDEO_BGR) |
		   (video->format == GLC_VIDEO_BGRA)) {
		color_generate_rgb_lookup_table(color, video);
		video->proc = &color_bgr;
	} else
		video->proc = NULL; /* don't attempt anything... */

	pthread_rwlock_unlock(&video->update);
	return 0;
}

void color_ycbcr(color_t color,
		 struct color_video_stream_s *video,
		 unsigned char *from, unsigned char *to)
{
	unsigned int x, y, Cpix, Y;
	unsigned int pos;
	unsigned char *Y_from, *Cb_from, *Cr_from;
	unsigned char *Y_to, *Cb_to, *Cr_to;

	Y_from = from;
	Cb_from = &from[video->h * video->w];
	Cr_from = &from[video->h * video->w + (video->h / 2) * (video->w / 2)];

	Y_to = to;
	Cb_to = &to[video->h * video->w];
	Cr_to = &to[video->h * video->w + (video->h / 2) * (video->w / 2)];

	Cpix = 0;

#define CONVERT_Y(xadd, yadd) 								\
	pos = YCBCR_LOOKUP_POS(Y_from[(x + (xadd)) + (y + (yadd)) * video->w],		\
			       Cb_from[Cpix], Cr_from[Cpix]);				\
	Y_to[(x + (xadd)) + (y + (yadd)) * video->w] = video->lookup_table[pos + 0];	\
	Y += video->lookup_table[pos + 0];

	for (y = 0; y < video->h; y += 2) {
		for (x = 0; x < video->w; x += 2) {
			Y = 0;

			CONVERT_Y(0, 0)
			CONVERT_Y(0, 1)
			CONVERT_Y(1, 0)
			CONVERT_Y(1, 1)

			pos = YCBCR_LOOKUP_POS(Y >> 2, Cb_from[Cpix], Cr_from[Cpix]);
			Cb_to[Cpix] = video->lookup_table[pos + 1];
			Cr_to[Cpix] = video->lookup_table[pos + 2];

			Cpix++;
		}
	}
}

void color_bgr(color_t color,
	       struct color_video_stream_s *video,
	       unsigned char *from, unsigned char *to)
{
	unsigned int x, y, p;

	for (y = 0; y < video->h; y++) {
		for (x = 0; x < video->w; x++) {
			p = video->row * y + x * video->bpp;

			to[p + 0] = video->lookup_table[256 + 256 + from[p + 0]];
			to[p + 1] = video->lookup_table[256       + from[p + 1]];
			to[p + 2] = video->lookup_table[            from[p + 2]];
		}
	}
}

#define CLAMP_256(val) \
	(val) < 0 ? 0 : ((val) > 255 ? 255 : (val))

unsigned char YCbCr_TO_RGB_Rd(unsigned char Y, unsigned char Cb, unsigned char Cr)
{
	int R = Y + 1.402 * (Cr - 128);
	return CLAMP_256(R);
}

unsigned char YCbCr_TO_RGB_Gd(unsigned char Y, unsigned char Cb, unsigned char Cr)
{
	int G = Y - 0.344136 * (Cb - 128) - 0.714136 * (Cr - 128);
	return CLAMP_256(G);
}

unsigned char YCbCr_TO_RGB_Bd(unsigned char Y, unsigned char Cb, unsigned char Cr)
{
	int B = Y + 1.772 * (Cb - 128);
	return CLAMP_256(B);
}

#define RGB_TO_YCbCrJPEG_Y(Rd, Gd, Bd) \
	(    + 0.299    * (Rd) +    0.587 * (Gd) + 0.114 *    (Bd))
#define RGB_TO_YCbCrJPEG_Cb(Rd, Gd, Bd) \
	(128 - 0.168736 * (Rd) - 0.331264 * (Gd) + 0.5      * (Bd))
#define RGB_TO_YCbCrJPEG_Cr(Rd, Gd, Bd) \
	(128 + 0.5      * (Rd) - 0.418688 * (Gd) - 0.081312 * (Bd))

int color_generate_ycbcr_lookup_table(color_t color,
				      struct color_video_stream_s *video)
{
	unsigned int Y, Cb, Cr, pos;
	unsigned char R, G, B;
	size_t lookup_size = (1 << LOOKUP_BITS) * (1 << LOOKUP_BITS) * (1 << LOOKUP_BITS) * 3;

	glc_log(color->glc, GLC_INFORMATION, "color",
		 "using %d bit lookup table (%zd bytes)", LOOKUP_BITS, lookup_size);
	video->lookup_table = malloc(lookup_size);

#define CALC(value, brightness, contrast, gamma) \
	((pow((double) value / 255.0, 1.0 / gamma) - 0.5) * (1.0 + contrast) \
	 + brightness + 0.5) * 255.0

	pos = 0;
	for (Y = 0; Y < 256; Y += (1 << (8 - LOOKUP_BITS))) {
		for (Cb = 0; Cb < 256; Cb += (1 << (8 - LOOKUP_BITS))) {
			for (Cr = 0; Cr < 256; Cr += (1 << (8 - LOOKUP_BITS))) {
				R = color_clamp(CALC(YCbCr_TO_RGB_Rd(Y, Cb, Cr),
						video->brightness, video->contrast, video->red_gamma));
				G = color_clamp(CALC(YCbCr_TO_RGB_Gd(Y, Cb, Cr),
						video->brightness, video->contrast, video->green_gamma));
				B = color_clamp(CALC(YCbCr_TO_RGB_Bd(Y, Cb, Cr),
						video->brightness, video->contrast, video->blue_gamma));

				video->lookup_table[pos + 0] = RGB_TO_YCbCrJPEG_Y(R, G, B);
				video->lookup_table[pos + 1] = RGB_TO_YCbCrJPEG_Cb(R, G, B);
				video->lookup_table[pos + 2] = RGB_TO_YCbCrJPEG_Cr(R, G, B);
				pos += 3;
			}
		}
	}

#undef CALC

	return 0;
}

int color_generate_rgb_lookup_table(color_t color,
				    struct color_video_stream_s *video)
{
	unsigned int c;

	video->lookup_table = malloc(256 + 256 + 256);

#define CALC(value, brightness, contrast, gamma) \
	color_clamp( \
		(((pow((double) value / 255.0, 1.0 / gamma) - 0.5) * (1.0 + contrast) + 0.5) \
		 + brightness) * 255.0 \
		)

	for (c = 0; c < 256; c++)
		video->lookup_table[c + 0] = CALC(c, video->brightness, video->contrast, video->red_gamma);

	for (c = 0; c < 256; c++)
		video->lookup_table[c + 256] = CALC(c, video->brightness, video->contrast, video->green_gamma);

	for (c = 0; c < 256; c++)
		video->lookup_table[c + 256 + 256] = CALC(c, video->brightness, video->contrast, video->blue_gamma);

#undef CALC

	return 0;
}

/**  \} */
