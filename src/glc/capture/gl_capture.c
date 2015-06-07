/**
 * \file glc/capture/gl_capture.c
 * \brief OpenGL capture
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup gl_capture
 *  \{
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/xf86vmode.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>
#include <unistd.h>
#include <packetstream.h>
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>
#include <endian.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/state.h>
#include <glc/common/util.h>

#include "gl_capture.h"

#define GL_CAPTURE_TRY_PBO          0x1
#define GL_CAPTURE_USE_PBO          0x2
#define GL_CAPTURE_CAPTURING        0x4
#define GL_CAPTURE_DRAW_INDICATOR   0x8
#define GL_CAPTURE_CROP            0x10
#define GL_CAPTURE_LOCK_FPS        0x20
#define GL_CAPTURE_IGNORE_TIME     0x40

// GL_UNSIGNED_BYTE is equal to
//   GL_UNSIGNED_INT_8_8_8_8_REV on little endian and
//   GL_UNSIGNED_INT_8_8_8_8 on big endian
// (ref: http://milkpot.sakura.ne.jp/gl/textureformat.html)
// Those are often faster than using GL_UNSIGNED_BYTE directly,
// however this optimization is only possible with 32bpp.
static const GLenum format_32bpp_alt =
#if __BYTE_ORDER == __LITTLE_ENDIAN
	GL_UNSIGNED_INT_8_8_8_8_REV
#else // __BIG_ENDIAN
	GL_UNSIGNED_INT_8_8_8_8
#endif
;

typedef void (*FuncPtr)(void);
typedef FuncPtr (*GLXGetProcAddressProc)(const GLubyte *procName);
typedef void (*glGenBuffersProc)(GLsizei n,
                                 GLuint *buffers);
typedef void (*glDeleteBuffersProc)(GLsizei n,
                                    const GLuint *buffers);
typedef void (*glBufferDataProc)(GLenum target,
                                 GLsizeiptr size,
                                 const GLvoid *data,
                                 GLenum usage);
typedef void (*glBindBufferProc)(GLenum target,
                                 GLuint buffer);
typedef GLvoid *(*glMapBufferProc)(GLenum target,
                                   GLenum access);
typedef GLboolean (*glUnmapBufferProc)(GLenum target);

struct gl_capture_video_stream_s {
	glc_state_video_t state_video;
	glc_stream_id_t id;

	glc_flags_t flags;
	glc_video_format_t format;
	Display *dpy;
	int screen;
	GLXDrawable drawable;
	Window attribWin;
	ps_packet_t packet;
	glc_utime_t last, pbo_time;

	unsigned int w, h;
	unsigned int cw, ch, row, cx, cy;

	float brightness, contrast;
	float gamma_red, gamma_green, gamma_blue;

	int indicator_list;

	struct gl_capture_video_stream_s *next;

	GLuint pbo;
	int pbo_active;
};

struct gl_capture_s {
	glc_t *glc;
	glc_flags_t flags;

	GLenum capture_buffer;
	glc_utime_t fps;

	pthread_rwlock_t videolist_lock;
	struct gl_capture_video_stream_s *video;

	ps_buffer_t *to;

	pthread_mutex_t init_pbo_mutex;

	unsigned int bpp;
	GLenum format;
	GLint pack_alignment;

	unsigned int crop_x, crop_y;
	unsigned int crop_w, crop_h;

	void *libGL_handle;
	GLXGetProcAddressProc glXGetProcAddress;
	glGenBuffersProc glGenBuffers;
	glDeleteBuffersProc glDeleteBuffers;
	glBufferDataProc glBufferData;
	glBindBufferProc glBindBuffer;
	glMapBufferProc glMapBuffer;
	glUnmapBufferProc glUnmapBuffer;

	char *last_frame;
	char *frame_temp;
	uint8_t intra_frame;
};

int gl_capture_get_video_stream(gl_capture_t gl_capture,
				struct gl_capture_video_stream_s **video,
				Display *dpy, GLXDrawable drawable);
int gl_capture_update_video_stream(gl_capture_t gl_capture,
				   struct gl_capture_video_stream_s *video);

void gl_capture_error(gl_capture_t gl_capture, int err);

int gl_capture_get_geometry(gl_capture_t gl_capture,
			    Display *dpy, Window win, unsigned int *w, unsigned int *h);
int gl_capture_calc_geometry(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video,
			     unsigned int w, unsigned int h);
int gl_capture_update_screen(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video);
int gl_capture_update_color(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video);

int gl_capture_get_pixels(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video, char *to);
int gl_capture_gen_indicator_list(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video);

int gl_capture_init_pbo(gl_capture_t gl);
int gl_capture_create_pbo(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video);
int gl_capture_destroy_pbo(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video);
int gl_capture_start_pbo(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video);
int gl_capture_read_pbo(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video);

int gl_capture_init(gl_capture_t *gl_capture, glc_t *glc)
{
	*gl_capture = (gl_capture_t) malloc(sizeof(struct gl_capture_s));
	memset(*gl_capture, 0, sizeof(struct gl_capture_s));

	(*gl_capture)->glc = glc;
	(*gl_capture)->fps = 1000000 / 30;		/* default fps is 30 */
	(*gl_capture)->pack_alignment = 8;		/* read as dword aligned by default */
	(*gl_capture)->format = GL_BGRA;		/* capture as BGRA data by default */
	(*gl_capture)->bpp = 4;				/* since we use BGRA */
	(*gl_capture)->capture_buffer = GL_FRONT;	/* front buffer is default */

	pthread_mutex_init(&(*gl_capture)->init_pbo_mutex, NULL);
	pthread_rwlock_init(&(*gl_capture)->videolist_lock, NULL);

	return 0;
}

int gl_capture_set_buffer(gl_capture_t gl_capture, ps_buffer_t *buffer)
{
	if (gl_capture->to)
		return EALREADY;

	gl_capture->to = buffer;
	return 0;
}

int gl_capture_set_read_buffer(gl_capture_t gl_capture, GLenum buffer)
{
	if (buffer == GL_FRONT)
		glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "reading frames from GL_FRONT");
	else if (buffer == GL_BACK)
		glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "reading frames from GL_BACK");
	else {
		glc_log(gl_capture->glc, GLC_ERROR, "gl_capture",
			 "unknown read buffer 0x%02x", buffer);
		return ENOTSUP;
	}

	gl_capture->capture_buffer = buffer;
	return 0;
}

int gl_capture_set_fps(gl_capture_t gl_capture, double fps)
{
	if (fps <= 0)
		return EINVAL;

	gl_capture->fps = 1000000 / fps;
	glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
		 "capturing at %f fps", fps);

	return 0;
}

int gl_capture_set_pack_alignment(gl_capture_t gl_capture, GLint pack_alignment)
{
	if (pack_alignment == 1)
		glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "reading data as byte aligned");
	else if (pack_alignment == 8)
		glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "reading data as dword aligned");
	else {
		glc_log(gl_capture->glc, GLC_ERROR, "gl_capture",
			 "unknown GL_PACK_ALIGNMENT %d", pack_alignment);
		return ENOTSUP;
	}

	gl_capture->pack_alignment = pack_alignment;
	return 0;
}

int gl_capture_try_pbo(gl_capture_t gl_capture, int try_pbo)
{
	if (try_pbo) {
		gl_capture->flags |= GL_CAPTURE_TRY_PBO;
	} else {
		if (gl_capture->flags & GL_CAPTURE_USE_PBO) {
			glc_log(gl_capture->glc, GLC_WARNING, "gl_capture",
				 "can't disable PBO; it is in use");
			return EAGAIN;
		}

		glc_log(gl_capture->glc, GLC_DEBUG, "gl_capture",
			 "PBO disabled");
		gl_capture->flags &= ~GL_CAPTURE_TRY_PBO;
	}

	return 0;
}

int gl_capture_set_pixel_format(gl_capture_t gl_capture, GLenum format)
{
	if (format == GL_BGRA) {
		glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "reading frames in GL_BGRA format");
		gl_capture->bpp = 4;
	} else if (format == GL_BGR) {
		glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "reading frames in GL_BGR format");
		gl_capture->bpp = 3;
	} else {
		glc_log(gl_capture->glc, GLC_ERROR, "gl_capture",
			 "unsupported pixel format 0x%02x", format);
		return ENOTSUP;
	}

	gl_capture->format = format;
	return 0;
}

int gl_capture_draw_indicator(gl_capture_t gl_capture, int draw_indicator)
{
	if (draw_indicator) {
		gl_capture->flags |= GL_CAPTURE_DRAW_INDICATOR;

		if (gl_capture->capture_buffer == GL_FRONT)
			glc_log(gl_capture->glc, GLC_WARNING, "gl_capture",
				 "indicator doesn't work well when capturing from GL_FRONT");
	} else
		gl_capture->flags &= ~GL_CAPTURE_DRAW_INDICATOR;

	return 0;
}

int gl_capture_ignore_time(gl_capture_t gl_capture, int ignore_time)
{
	if (ignore_time)
		gl_capture->flags |= GL_CAPTURE_IGNORE_TIME;
	else
		gl_capture->flags &= ~GL_CAPTURE_IGNORE_TIME;
	return 0;
}

int gl_capture_crop(gl_capture_t gl_capture, unsigned int x, unsigned int y,
			     unsigned int width, unsigned int height)
{
	if ((!x) && (!y) && (!width) && (!height)) {
		gl_capture->flags &= ~GL_CAPTURE_CROP;
		return 0;
	}

	gl_capture->crop_x = x;
	gl_capture->crop_y = y;
	gl_capture->crop_w = width;
	gl_capture->crop_h = height;
	gl_capture->flags |= GL_CAPTURE_CROP;

	return 0;
}

int gl_capture_lock_fps(gl_capture_t gl_capture, int lock_fps)
{
	if (lock_fps)
		gl_capture->flags |= GL_CAPTURE_LOCK_FPS;
	else
		gl_capture->flags &= ~GL_CAPTURE_LOCK_FPS;

	return 0;
}

int gl_capture_start(gl_capture_t gl_capture)
{
	if (!gl_capture->to) {
		glc_log(gl_capture->glc, GLC_ERROR, "gl_capture",
			 "no target buffer specified");
		return EAGAIN;
	}

	if (gl_capture->flags & GL_CAPTURE_CAPTURING)
		glc_log(gl_capture->glc, GLC_WARNING, "gl_capture",
			 "capturing is already active");
	else
		glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "starting capturing");

	gl_capture->flags |= GL_CAPTURE_CAPTURING;
	gl_capture_refresh_color_correction(gl_capture);
	return 0;
}

int gl_capture_stop(gl_capture_t gl_capture)
{
	if (gl_capture->flags & GL_CAPTURE_CAPTURING)
		glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "stopping capturing");
	else
		glc_log(gl_capture->glc, GLC_WARNING, "gl_capture",
			 "capturing is already stopped");

	gl_capture->flags &= ~GL_CAPTURE_CAPTURING;
	return 0;
}

void gl_capture_error(gl_capture_t gl_capture, int err)
{
	glc_log(gl_capture->glc, GLC_ERROR, "gl_capture",
		"%s (%d)", strerror(err), err);

	/* stop capturing */
	if (gl_capture->flags & GL_CAPTURE_CAPTURING)
		gl_capture_stop(gl_capture);

	/* cancel glc */
	glc_state_set(gl_capture->glc, GLC_STATE_CANCEL);
	if (gl_capture->to)
		ps_buffer_cancel(gl_capture->to);
}

int gl_capture_destroy(gl_capture_t gl_capture)
{
	struct gl_capture_video_stream_s *del;

	while (gl_capture->video != NULL) {
		del = gl_capture->video;
		gl_capture->video = gl_capture->video->next;
		
		/* we might be in wrong thread */
		if (del->indicator_list)
			glDeleteLists(del->indicator_list, 1);

		if (del->pbo)
			gl_capture_destroy_pbo(gl_capture, del);

		ps_packet_destroy(&del->packet);
		free(del);
	}

	pthread_rwlock_destroy(&gl_capture->videolist_lock);
	pthread_mutex_destroy(&gl_capture->init_pbo_mutex);

	if (gl_capture->libGL_handle)
		dlclose(gl_capture->libGL_handle);
	free(gl_capture);

	return 0;
}

int gl_capture_get_geometry(gl_capture_t gl_capture, Display *dpy, Window win,
                    unsigned int *w, unsigned int *h)
{
	Window rootWindow;
	int unused;

	XGetGeometry(dpy, win, &rootWindow, &unused, &unused, w, h,
	             (unsigned int *) &unused, (unsigned int *) &unused);
	
	return 0;
}

int gl_capture_update_screen(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video)
{
	/** \todo figure out real screen */
	video->screen = DefaultScreen(video->dpy);
	return 0;
}

int gl_capture_calc_geometry(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video,
			     unsigned int w, unsigned int h)
{
	video->w = w;
	video->h = h;

	/* calculate image area when cropping */
	if (gl_capture->flags & GL_CAPTURE_CROP) {
		if (gl_capture->crop_x > video->w)
			video->cx = 0;
		else
			video->cx = gl_capture->crop_x;

		if (gl_capture->crop_y > video->h)
			video->cy = 0;
		else
			video->cy = gl_capture->crop_y;

		if (gl_capture->crop_w + video->cx > video->w)
			video->cw = video->w - video->cx;
		else
			video->cw = gl_capture->crop_w;

		if (gl_capture->crop_h + video->cy > video->h)
			video->ch = video->h - video->cy;
		else
			video->ch = gl_capture->crop_h;

		/* we need to recalc y coord for OpenGL */
		video->cy = video->h - video->ch - video->cy;
	} else {
		video->cw = video->w;
		video->ch = video->h;
		video->cx = video->cy = 0;
	}

	glc_log(gl_capture->glc, GLC_DEBUG, "gl_capture",
		 "calculated capture area for video %d is %ux%u+%u+%u",
		 video->id, video->cw, video->ch, video->cx, video->cy);

	video->row = video->cw * gl_capture->bpp;
	if (video->row % gl_capture->pack_alignment != 0)
		video->row += gl_capture->pack_alignment - video->row % gl_capture->pack_alignment;

	return 0;
}

int gl_capture_get_pixels(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video, char *to)
{
	glPushAttrib(GL_PIXEL_MODE_BIT);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

	glReadBuffer(gl_capture->capture_buffer);
	glPixelStorei(GL_PACK_ALIGNMENT, gl_capture->pack_alignment);
	if (gl_capture->bpp == 4)
		glReadPixels(video->cx, video->cy, video->cw, video->ch, gl_capture->format, format_32bpp_alt, to);
	else
		glReadPixels(video->cx, video->cy, video->cw, video->ch, gl_capture->format, GL_UNSIGNED_BYTE, to);
	glPopClientAttrib();
	glPopAttrib();

	return 0;
}

int gl_capture_gen_indicator_list(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video)
{
	int size;
	if (!video->indicator_list)
		video->indicator_list = glGenLists(1);
	
	glNewList(video->indicator_list, GL_COMPILE);
	
	size = video->h / 75;
	if (size < 10)
		size = 10;

	glPushAttrib(GL_ALL_ATTRIB_BITS);

	glViewport(0, 0, video->w, video->h);
	glEnable(GL_SCISSOR_TEST);
	glScissor(size / 2 - 1, video->h - size - size / 2 - 1, size + 2, size + 2);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glScissor(size / 2, video->h - size - size / 2, size, size);
	glClearColor(1.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_SCISSOR_TEST);

	glPopAttrib();

	glEndList();
	return 0;
}

int gl_capture_init_pbo(gl_capture_t gl_capture)
{
	const char *gl_extensions = (const char *) glGetString(GL_EXTENSIONS);

	if (gl_extensions == NULL)
		return EINVAL;
	
	if (!strstr(gl_extensions, "GL_ARB_pixel_buffer_object"))
		return ENOTSUP;
	
	gl_capture->libGL_handle = dlopen("libGL.so.1", RTLD_LAZY);
	if (!gl_capture->libGL_handle)
		return ENOTSUP;
	gl_capture->glXGetProcAddress =
		(GLXGetProcAddressProc)
		dlsym(gl_capture->libGL_handle, "glXGetProcAddressARB");
	if (!gl_capture->glXGetProcAddress)
		return ENOTSUP;
	
	gl_capture->glGenBuffers =
		(glGenBuffersProc)
		gl_capture->glXGetProcAddress((const GLubyte *) "glGenBuffersARB");
	if (!gl_capture->glGenBuffers)
		return ENOTSUP;
	gl_capture->glDeleteBuffers =
		(glDeleteBuffersProc)
		gl_capture->glXGetProcAddress((const GLubyte *) "glDeleteBuffersARB");
	if (!gl_capture->glDeleteBuffers)
		return ENOTSUP;
	gl_capture->glBufferData =
		(glBufferDataProc)
		gl_capture->glXGetProcAddress((const GLubyte *) "glBufferDataARB");
	if (!gl_capture->glBufferData)
		return ENOTSUP;
	gl_capture->glBindBuffer =
		(glBindBufferProc)
		gl_capture->glXGetProcAddress((const GLubyte *) "glBindBufferARB");
	if (!gl_capture->glBindBuffer)
		return ENOTSUP;
	gl_capture->glMapBuffer =
		(glMapBufferProc)
		gl_capture->glXGetProcAddress((const GLubyte *) "glMapBufferARB");
	if (!gl_capture->glMapBuffer)
		return ENOTSUP;
	gl_capture->glUnmapBuffer =
		(glUnmapBufferProc)
		gl_capture->glXGetProcAddress((const GLubyte *) "glUnmapBufferARB");
	if (!gl_capture->glUnmapBuffer)
		return ENOTSUP;

	glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
		 "using GL_ARB_pixel_buffer_object");

	return 0;
}

int gl_capture_create_pbo(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video)
{
	GLint binding;

	glc_log(gl_capture->glc, GLC_DEBUG, "gl_capture", "creating PBO");

	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_ARB, &binding);
	glPushAttrib(GL_ALL_ATTRIB_BITS);

	gl_capture->glGenBuffers(1, &video->pbo);
	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, video->pbo);
	gl_capture->glBufferData(GL_PIXEL_PACK_BUFFER_ARB, video->row * video->ch,
		         NULL, GL_STREAM_READ);

	glPopAttrib();
	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, binding);
	return 0;
}

int gl_capture_destroy_pbo(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video)
{
	glc_log(gl_capture->glc, GLC_DEBUG, "gl_capture", "destroying PBO");

	gl_capture->glDeleteBuffers(1, &video->pbo);
	return 0;
}

int gl_capture_start_pbo(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video)
{
	GLint binding;

	if (video->pbo_active)
		return EAGAIN;

	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_ARB, &binding);
	glPushAttrib(GL_PIXEL_MODE_BIT);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, video->pbo);

	glReadBuffer(gl_capture->capture_buffer);
	glPixelStorei(GL_PACK_ALIGNMENT, gl_capture->pack_alignment);
	/* to = ((char *)NULL + (offset)) */
	if (gl_capture->bpp == 4)
		glReadPixels(video->cx, video->cy, video->cw, video->ch, gl_capture->format, format_32bpp_alt, NULL);
	else
		glReadPixels(video->cx, video->cy, video->cw, video->ch, gl_capture->format, GL_UNSIGNED_BYTE, NULL);

	video->pbo_active = 1;

	glPopClientAttrib();
	glPopAttrib();
	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, binding);
	return 0;
}

int gl_capture_read_pbo(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video)
{
	GLvoid *buf;
	GLint binding;
	
	if (!video->pbo_active)
		return EAGAIN;

	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_ARB, &binding);

	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, video->pbo);
	buf = gl_capture->glMapBuffer(GL_PIXEL_PACK_BUFFER_ARB, GL_READ_ONLY);
	if (!buf)
		return EINVAL;

	ps_packet_write(&video->packet, buf, video->row * video->ch);

	gl_capture->glUnmapBuffer(GL_PIXEL_PACK_BUFFER_ARB);

	video->pbo_active = 0;
	
	gl_capture->glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, binding);
	return 0;
}

int gl_capture_get_video_stream(gl_capture_t gl_capture, struct gl_capture_video_stream_s **video, Display *dpy, GLXDrawable drawable)
{
	struct gl_capture_video_stream_s *fvideo;

	pthread_rwlock_rdlock(&gl_capture->videolist_lock);
	fvideo = gl_capture->video;
	while (fvideo != NULL) {
		if ((fvideo->drawable == drawable) && (fvideo->dpy == dpy))
			break;
		
		fvideo = fvideo->next;
	}
	pthread_rwlock_unlock(&gl_capture->videolist_lock);
	
	if (fvideo == NULL) {
		fvideo = (struct gl_capture_video_stream_s *) malloc(sizeof(struct gl_capture_video_stream_s));
		memset(fvideo, 0, sizeof(struct gl_capture_video_stream_s));

		fvideo->dpy = dpy;
		fvideo->drawable = drawable;
		ps_packet_init(&fvideo->packet, gl_capture->to);

		glc_state_video_new(gl_capture->glc, &fvideo->id, &fvideo->state_video);

		/* these functions need to be thread-safe */
		pthread_rwlock_wrlock(&gl_capture->videolist_lock);

		fvideo->next = gl_capture->video;
		gl_capture->video = fvideo;

		pthread_rwlock_unlock(&gl_capture->videolist_lock);
	}
	
	*video = fvideo;
	return 0;
}

int gl_capture_update_video_stream(gl_capture_t gl_capture,
			  struct gl_capture_video_stream_s *video)
{
	glc_message_header_t msg;
	glc_video_format_message_t format_msg;
	unsigned int w, h;

	/* initialize PBO if not already done */
	if ((!(gl_capture->flags & GL_CAPTURE_USE_PBO)) &&
	    (gl_capture->flags & GL_CAPTURE_TRY_PBO)) {
		pthread_mutex_lock(&gl_capture->init_pbo_mutex);

		if (!gl_capture_init_pbo(gl_capture))
			gl_capture->flags |= GL_CAPTURE_USE_PBO;
		else
			gl_capture->flags &= ~GL_CAPTURE_TRY_PBO;

		pthread_mutex_unlock(&gl_capture->init_pbo_mutex);
	}

	gl_capture_get_geometry(gl_capture, video->dpy,
				video->attribWin ? video->attribWin : video->drawable,
				&w, &h);

	if (!video->format) {
		/* initialize screen information */
		gl_capture_update_screen(gl_capture, video);

		/* reset gamma values */
		video->gamma_red = video->gamma_green = video->gamma_blue = 1.0;

		if (gl_capture->format == GL_BGRA)
			video->format = GLC_VIDEO_BGRA;
		else if (gl_capture->format == GL_BGR)
			video->format = GLC_VIDEO_BGR;
		else
			return EINVAL;

		if (gl_capture->pack_alignment == 8)
			video->flags |= GLC_VIDEO_DWORD_ALIGNED;
	}

	if ((w != video->w) | (h != video->h)) {
		gl_capture_calc_geometry(gl_capture, video, w, h);

		glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "creating/updating configuration for video %d", video->id);

		msg.type = GLC_MESSAGE_VIDEO_FORMAT;
		format_msg.flags = video->flags;
		format_msg.format = video->format;
		format_msg.id = video->id;
		format_msg.width = video->cw;
		format_msg.height = video->ch;

		ps_packet_open(&video->packet, PS_PACKET_WRITE);
		ps_packet_write(&video->packet, &msg, sizeof(glc_message_header_t));
		ps_packet_write(&video->packet, &format_msg, sizeof(glc_video_format_message_t));
		ps_packet_close(&video->packet);

		glc_log(gl_capture->glc, GLC_DEBUG, "gl_capture",
			 "video %d: %ux%u (%ux%u), 0x%02x flags", video->id,
			 video->cw, video->ch, video->w, video->h, video->flags);

		/* how about color correction? */
		gl_capture_update_color(gl_capture, video);

		if (gl_capture->flags & GL_CAPTURE_USE_PBO) {
			if (video->pbo)
				gl_capture_destroy_pbo(gl_capture, video);

			if (gl_capture_create_pbo(gl_capture, video)) {
				gl_capture->flags &= ~(GL_CAPTURE_TRY_PBO | GL_CAPTURE_USE_PBO);
				/** \todo destroy pbo stuff? */
				/** \todo race condition? */
			}
		}

		if(gl_capture->last_frame)
			free(gl_capture->last_frame);
		gl_capture->last_frame = malloc(video->row * video->ch);
		if(gl_capture->frame_temp)
			free(gl_capture->frame_temp);
		gl_capture->frame_temp = malloc(video->row * video->ch);
		gl_capture->intra_frame = 1;
	}


	if ((gl_capture->flags & GL_CAPTURE_DRAW_INDICATOR) &&
	    (!video->indicator_list))
		gl_capture_gen_indicator_list(gl_capture, video);

	return 0;
}

#define ISALIGNED(ptr, n) (((uintptr_t) (ptr) & ((n) - 1)) == 0)
void memxor(void *dest, const void *a, const void *b, size_t size)
{ // dest[0..size] = a[0..size] ^ b[0..size]
	const void *end = a + size;
	const void *p0;
	const void *p1;
	void *pd;
	const uint8_t *b0 = (const uint8_t*) a;
	const uint8_t *b1 = (const uint8_t*) b;
	uint8_t *bd = (uint8_t*) dest;

	// Having matching alignment is very unlikely
	// b0 and b1 happen to have matching alignment on my machine so this code tries to align those
	for(; (!ISALIGNED(b0, 32) || !ISALIGNED(b1, 32)) && b0 < (const uint8_t*) end; b0++, b1++, bd++) {
		*bd = *b0 ^ *b1;
	}

	for(p0 = b0, p1 = b1, pd = bd; p0 < end; p0 += 32, p1 += 32, pd += 32) {
		__asm__(
			"vmovdqa (%1), %%ymm1;"
			"vxorpd (%0), %%ymm1, %%ymm1;"
			"vmovdqu %%ymm1, (%2);"
			: /* no outputs */
			: "r" (p0), "r" (p1), "r" (pd)
			: "ymm1"
		);
	}

	for(b0 = (const uint8_t*) p0, b1 = (const uint8_t*) p1, bd = (uint8_t*) pd; b0 < (const uint8_t*) end; b0++, b1++, bd++) {
		*bd = *b0 ^ *b1;
	}
}

#define SWAP(a, b, t) \
	do { \
		t _tmp; \
		_tmp = (a); \
		(a) = (b); \
		(b) = _tmp; \
	} while(0);

int gl_capture_frame(gl_capture_t gl_capture, Display *dpy, GLXDrawable drawable)
{
	struct gl_capture_video_stream_s *video;
	glc_message_header_t msg;
	glc_video_frame_header_t pic;
	glc_utime_t now;
	char *dma;
	int ret = 0;

	if (!(gl_capture->flags & GL_CAPTURE_CAPTURING))
		return 0; /* capturing not active */

	gl_capture_get_video_stream(gl_capture, &video, dpy, drawable);

	msg.type = GLC_MESSAGE_VIDEO_FRAME;
	pic.id = video->id;

	/* get current time */
	if (gl_capture->flags & GL_CAPTURE_IGNORE_TIME)
		now = video->last + gl_capture->fps;
	else
		now = glc_state_time(gl_capture->glc);

	/* if we are using PBO we will actually write previous picture to buffer */
	if (gl_capture->flags & GL_CAPTURE_USE_PBO)
		pic.time = video->pbo_time;
	else
		pic.time = now;

	/* has gl_capture->fps microseconds elapsed since last capture */
	if ((now - video->last < gl_capture->fps) &&
	    !(gl_capture->flags & GL_CAPTURE_LOCK_FPS) &&
	    !(gl_capture->flags & GL_CAPTURE_IGNORE_TIME))
		goto finish;

	/* not really needed until now */
	if ((ret = gl_capture_update_video_stream(gl_capture, video)))
		goto finish;

	/* if PBO is not active, just start transfer and finish */
	if ((gl_capture->flags & GL_CAPTURE_USE_PBO) && (!video->pbo_active)) {
		ret = gl_capture_start_pbo(gl_capture, video);
		video->pbo_time = now;

		goto finish;
	}

	if (ps_packet_open(&video->packet, ((gl_capture->flags & GL_CAPTURE_LOCK_FPS) |
					    (gl_capture->flags & GL_CAPTURE_IGNORE_TIME)) ?
					   (PS_PACKET_WRITE) :
					   (PS_PACKET_WRITE | PS_PACKET_TRY)))
		goto finish;
	if ((ret = ps_packet_write(&video->packet, &msg, sizeof(glc_message_header_t))))
		goto cancel;
	if ((ret = ps_packet_write(&video->packet, &pic, sizeof(glc_video_frame_header_t))))
		goto cancel;

	if (gl_capture->flags & GL_CAPTURE_USE_PBO) {
		/* is this safe, what happens if this is called simultaneously? */
		if ((ret = ps_packet_setsize(&video->packet, video->row * video->ch
								+ sizeof(glc_message_header_t)
								+ sizeof(glc_video_frame_header_t))))
			goto cancel;

		if ((ret = gl_capture_read_pbo(gl_capture, video)))
			goto cancel;

		ret = gl_capture_start_pbo(gl_capture, video);
		video->pbo_time = now;
	} else {
		if ((ret = ps_packet_dma(&video->packet, (void *) &dma,
					video->row * video->ch, PS_ACCEPT_FAKE_DMA)))
		goto cancel;

		if(gl_capture->intra_frame == 1) {
			ret = gl_capture_get_pixels(gl_capture, video, gl_capture->last_frame);
			memcpy(dma, gl_capture->last_frame, video->row * video->ch);
			gl_capture->intra_frame = 0;
		} else {
			ret = gl_capture_get_pixels(gl_capture, video, gl_capture->frame_temp);
			memxor(dma, gl_capture->frame_temp, gl_capture->last_frame, video->row * video->ch);
			SWAP(gl_capture->frame_temp, gl_capture->last_frame, char*);
		}
	}

	if ((gl_capture->flags & GL_CAPTURE_LOCK_FPS) &&
	    !(gl_capture->flags & GL_CAPTURE_IGNORE_TIME)) {
		now = glc_state_time(gl_capture->glc);

		if (now - video->last < gl_capture->fps)
			usleep(gl_capture->fps + video->last - now);
	}

	/* increment by 1/fps seconds */
	video->last += gl_capture->fps;

	/*
	 We should accept framedrops (eg. not allow this difference
	 to grow unlimited.
	*/
	if (!(gl_capture->flags & GL_CAPTURE_IGNORE_TIME)) {
		now = glc_state_time(gl_capture->glc);

		if (now - video->last > gl_capture->fps) /* reasonable choice? */
			video->last = now - 0.5 * gl_capture->fps;
	}

	ps_packet_close(&video->packet);

finish:
	if (ret != 0)
		gl_capture_error(gl_capture, ret);

	if (gl_capture->flags & GL_CAPTURE_DRAW_INDICATOR)
		glCallList(video->indicator_list);

	return ret;
cancel:
	if (ret == EBUSY) {
		ret = 0;
		glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
			 "dropped frame, buffer not ready");
	}
	ps_packet_cancel(&video->packet);
	goto finish;
}

int gl_capture_refresh_color_correction(gl_capture_t gl_capture)
{
	struct gl_capture_video_stream_s *video;

	if (!(gl_capture->flags & GL_CAPTURE_CAPTURING))
		return 0; /* capturing not active */

	glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
		 "refreshing color correction");

	pthread_rwlock_rdlock(&gl_capture->videolist_lock);
	video = gl_capture->video;
	while (video != NULL) {
		gl_capture_update_color(gl_capture, video);
		
		video = video->next;
	}
	pthread_rwlock_unlock(&gl_capture->videolist_lock);

	return 0;
}

/** \todo support GammaRamp */
int gl_capture_update_color(gl_capture_t gl_capture, struct gl_capture_video_stream_s *video)
{
	glc_message_header_t msg_hdr;
	glc_color_message_t msg;
	XF86VidModeGamma gamma;
	int ret = 0;

	XF86VidModeGetGamma(video->dpy, video->screen, &gamma);

	if ((gamma.red == video->gamma_red) &&
	    (gamma.green == video->gamma_green) &&
	    (gamma.blue == video->gamma_blue))
		return 0; /* nothing to update */

	msg_hdr.type = GLC_MESSAGE_COLOR;
	msg.id = video->id;
	msg.red = gamma.red;
	msg.green = gamma.green;
	msg.blue = gamma.blue;

	/** \todo figure out brightness and contrast */
	msg.brightness = msg.contrast = 0;

	glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
		 "color correction: brightness=%f, contrast=%f, red=%f, green=%f, blue=%f",
		 msg.brightness, msg.contrast, msg.red, msg.green, msg.blue);

	if ((ret = ps_packet_open(&video->packet, PS_PACKET_WRITE)))
		goto err;
	if ((ret = ps_packet_write(&video->packet, &msg_hdr, sizeof(glc_message_header_t))))
		goto err;
	if ((ret = ps_packet_write(&video->packet, &msg, sizeof(glc_color_message_t))))
		goto err;
	if ((ret = ps_packet_close(&video->packet)))
		goto err;

	return 0;

err:
	ps_packet_cancel(&video->packet);

	glc_log(gl_capture->glc, GLC_ERROR, "gl_capture",
		 "can't write gamma correction information to buffer: %s (%d)",
		 strerror(ret), ret);
	return ret;
}

int gl_capture_set_attribute_window(gl_capture_t gl_capture, Display *dpy,
				    GLXDrawable drawable, Window window)
{
	struct gl_capture_video_stream_s *video;
	gl_capture_get_video_stream(gl_capture, &video, dpy, drawable);

	glc_log(gl_capture->glc, GLC_INFORMATION, "gl_capture",
		"setting attribute window %p for drawable %p", (void *) window, (void *) drawable);
	video->attribWin = window;
	return 0;
}

/**  \} */
