/**
 * \file glc/core/pack.c
 * \brief stream compression
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup pack
 *  \{
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#include <glc/common/glc.h>
#include <glc/common/core.h>
#include <glc/common/log.h>
#include <glc/common/thread.h>
#include <glc/common/util.h>

#include "pack.h"

#ifdef __MINILZO
# include <minilzo.h>
# define __lzo_compress lzo1x_1_compress
# define __lzo_decompress lzo1x_decompress
# define __lzo_worstcase(size) size + (size / 16) + 64 + 3
# define __lzo_wrk_mem LZO1X_1_MEM_COMPRESS
# define __LZO
#elif defined __LZO
# include <lzo/lzo1x.h>
# define __lzo_compress lzo1x_1_11_compress
# define __lzo_decompress lzo1x_decompress
# define __lzo_worstcase(size) size + (size / 16) + 64 + 3
# define __lzo_wrk_mem LZO1X_1_11_MEM_COMPRESS
#endif

#ifdef __QUICKLZ
# include <quicklz.h>
#endif

#ifdef __LZJB
# include <lzjb.h>
#endif

struct pack_s {
	glc_t *glc;
	glc_thread_t thread;
	size_t compress_min;
	int running;
	int compression;
};

struct unpack_s {
	glc_t *glc;
	glc_thread_t thread;
	int running;
};

int pack_thread_create_callback(void *ptr, void **threadptr);
void pack_thread_finish_callback(void *ptr, void *threadptr, int err);
int pack_read_callback(glc_thread_state_t *state);
int pack_quicklz_write_callback(glc_thread_state_t *state);
int pack_lzo_write_callback(glc_thread_state_t *state);
int pack_lzjb_write_callback(glc_thread_state_t *state);
void pack_finish_callback(void *ptr, int err);

int unpack_read_callback(glc_thread_state_t *state);
int unpack_write_callback(glc_thread_state_t *state);
void unpack_finish_callback(void *ptr, int err);

int pack_init(pack_t *pack, glc_t *glc)
{
	*pack = (pack_t) malloc(sizeof(struct pack_s));
	memset(*pack, 0, sizeof(struct pack_s));

	(*pack)->glc = glc;
	(*pack)->compress_min = 1024;

	(*pack)->thread.flags = GLC_THREAD_WRITE | GLC_THREAD_READ;
	(*pack)->thread.ptr = *pack;
	(*pack)->thread.thread_create_callback = &pack_thread_create_callback;
	(*pack)->thread.thread_finish_callback = &pack_thread_finish_callback;
	(*pack)->thread.read_callback = &pack_read_callback;
	(*pack)->thread.finish_callback = &pack_finish_callback;
	(*pack)->thread.threads = glc_threads_hint(glc);

#ifdef __QUICKLZ
	pack_set_compression(*pack, PACK_QUICKLZ);
#elif defined __LZO
	pack_set_compression(*pack, PACK_LZO);
#elif defined __LZJB
	pack_set_compression(*pack, PACK_LZJB);
#else
	glc_log((*pack)->glc, GLC_ERROR, "pack",
		 "no supported compression algorithms found");
	return ENOTSUP;
#endif

	return 0;
}

int pack_set_compression(pack_t pack, int compression)
{
	if (pack->running)
		return EALREADY;

	if (compression == PACK_QUICKLZ) {
#ifdef __QUICKLZ
		pack->thread.write_callback = &pack_quicklz_write_callback;
		glc_log(pack->glc, GLC_INFORMATION, "pack",
			 "compressing using QuickLZ");
#else
		glc_log(pack->glc, GLC_ERROR, "pack",
			 "QuickLZ not supported");
		return ENOTSUP;
#endif
	} else if (compression == PACK_LZO) {
#ifdef __LZO
		pack->thread.write_callback = &pack_lzo_write_callback;
		glc_log(pack->glc, GLC_INFORMATION, "pack",
			 "compressing using LZO");
		lzo_init();
#else
		glc_log(pack->glc, GLC_ERROR, "pack",
			 "LZO not supported");
		return ENOTSUP;
#endif
	} else if (compression == PACK_LZJB) {
#ifdef __LZJB
		pack->thread.write_callback = &pack_lzjb_write_callback;
		glc_log(pack->glc, GLC_INFORMATION, "pack",
			"compressing using LZJB");
#else
		glc_log(pack->glc, GLC_ERROR, "pack",
			"LZJB not supported");
		return ENOTSUP;
#endif
	} else {
		glc_log(pack->glc, GLC_ERROR, "pack",
			 "unknown/unsupported compression algorithm 0x%02x",
			 compression);
		return ENOTSUP;
	}

	pack->compression = compression;
	return 0;
}

int pack_set_minimum_size(pack_t pack, ssize_t min_size)
{
	if (pack->running)
		return EALREADY;

	if (min_size < 0)
		return EINVAL;

	pack->compress_min = min_size;
	return 0;
}

int pack_process_start(pack_t pack, ps_buffer_t *from, ps_buffer_t *to)
{
	int ret;
	if (pack->running)
		return EAGAIN;

	if ((ret = glc_thread_create(pack->glc, &pack->thread, from, to)))
		return ret;
	pack->running = 1;

	return 0;
}

int pack_process_wait(pack_t pack)
{
	if (!pack->running)
		return EAGAIN;

	glc_thread_wait(&pack->thread);
	pack->running = 0;

	return 0;
}

int pack_destroy(pack_t pack)
{
	free(pack);
	return 0;
}

void pack_finish_callback(void *ptr, int err)
{
	pack_t pack = (pack_t) ptr;

	if (err)
		glc_log(pack->glc, GLC_ERROR, "pack", "%s (%d)", strerror(err), err);
}

int pack_thread_create_callback(void *ptr, void **threadptr)
{
	pack_t pack = (pack_t) ptr;

	if (pack->compression == PACK_QUICKLZ) {
#ifdef __QUICKLZ
		*threadptr = malloc(__quicklz_hashtable);
#endif
	} else if (pack->compression == PACK_LZO) {
#ifdef __LZO
		*threadptr = malloc(__lzo_wrk_mem);
#endif
	}

	return 0;
}

void pack_thread_finish_callback(void *ptr, void *threadptr, int err)
{
	if (threadptr)
		free(threadptr);
}

int pack_read_callback(glc_thread_state_t *state)
{
	pack_t pack = (pack_t) state->ptr;

	/* compress only audio and pictures */
	if ((state->read_size > pack->compress_min) &&
	    ((state->header.type == GLC_MESSAGE_VIDEO_FRAME) |
	     (state->header.type == GLC_MESSAGE_AUDIO_DATA))) {
		if (pack->compression == PACK_QUICKLZ) {
#ifdef __QUICKLZ
			state->write_size = sizeof(glc_container_message_header_t)
					    + sizeof(glc_quicklz_header_t)
					    + __quicklz_worstcase(state->read_size);
#else
			goto copy;
#endif
		} else if (pack->compression == PACK_LZO) {
#ifdef __LZO
			state->write_size = sizeof(glc_container_message_header_t)
					    + sizeof(glc_lzo_header_t)
			                    + __lzo_worstcase(state->read_size);
#else
			goto copy;
#endif
		} else if (pack->compression == PACK_LZJB) {
#ifdef __LZJB
			state->write_size = sizeof(glc_container_message_header_t)
					    + sizeof(glc_lzjb_header_t)
					    + __lzjb_worstcase(state->read_size);
#else
			goto copy;
#endif
		} else
			goto copy;

		return 0;
	}
copy:
	state->flags |= GLC_THREAD_COPY;
	return 0;
}

int pack_lzo_write_callback(glc_thread_state_t *state)
{
#ifdef __LZO
	glc_container_message_header_t *container = (glc_container_message_header_t *) state->write_data;
	glc_lzo_header_t *lzo_header =
		(glc_lzo_header_t *) &state->write_data[sizeof(glc_container_message_header_t)];
	lzo_uint compressed_size;

	__lzo_compress((unsigned char *) state->read_data, state->read_size,
		       (unsigned char *) &state->write_data[sizeof(glc_lzo_header_t) +
		       					    sizeof(glc_container_message_header_t)],
		       &compressed_size, (lzo_voidp) state->threadptr);

	lzo_header->size = (glc_size_t) state->read_size;
	memcpy(&lzo_header->header, &state->header, sizeof(glc_message_header_t));

	container->size = compressed_size + sizeof(glc_lzo_header_t);
	container->header.type = GLC_MESSAGE_LZO;

	state->header.type = GLC_MESSAGE_CONTAINER;

	return 0;
#else
	return ENOTSUP;
#endif
}

int pack_quicklz_write_callback(glc_thread_state_t *state)
{
#ifdef __QUICKLZ
	glc_container_message_header_t *container = (glc_container_message_header_t *) state->write_data;
	glc_quicklz_header_t *quicklz_header =
		(glc_quicklz_header_t *) &state->write_data[sizeof(glc_container_message_header_t)];
	size_t compressed_size;

	quicklz_compress((const unsigned char *) state->read_data,
			 (unsigned char *) &state->write_data[sizeof(glc_quicklz_header_t) +
			 				      sizeof(glc_container_message_header_t)],
			 state->read_size, &compressed_size,
			 (uintptr_t *) state->threadptr);

	quicklz_header->size = (glc_size_t) state->read_size;
	memcpy(&quicklz_header->header, &state->header, sizeof(glc_message_header_t));

	container->size = compressed_size + sizeof(glc_quicklz_header_t);
	container->header.type = GLC_MESSAGE_QUICKLZ;

	state->header.type = GLC_MESSAGE_CONTAINER;

	return 0;
#else
	return ENOTSUP;
#endif
}

int pack_lzjb_write_callback(glc_thread_state_t *state)
{
#ifdef __LZJB
	glc_container_message_header_t *container = (glc_container_message_header_t *) state->write_data;
	glc_lzjb_header_t *lzjb_header =
		(glc_lzjb_header_t *) &state->write_data[sizeof(glc_container_message_header_t)];

	size_t compressed_size = lzjb_compress(state->read_data,
					       &state->write_data[sizeof(glc_lzjb_header_t) +
					       			  sizeof(glc_container_message_header_t)],
					       state->read_size);

	lzjb_header->size = (glc_size_t) state->read_size;
	memcpy(&lzjb_header->header, &state->header, sizeof(glc_message_header_t));

	container->size = compressed_size + sizeof(glc_lzjb_header_t);
	container->header.type = GLC_MESSAGE_LZJB;

	state->header.type = GLC_MESSAGE_CONTAINER;

	return 0;
#else
	return ENOTSUP;
#endif
}

int unpack_init(unpack_t *unpack, glc_t *glc)
{
	*unpack = (unpack_t) malloc(sizeof(struct unpack_s));
	memset(*unpack, 0, sizeof(struct unpack_s));

	(*unpack)->glc = glc;

	(*unpack)->thread.flags = GLC_THREAD_WRITE | GLC_THREAD_READ;
	(*unpack)->thread.ptr = *unpack;
	(*unpack)->thread.read_callback = &unpack_read_callback;
	(*unpack)->thread.write_callback = &unpack_write_callback;
	(*unpack)->thread.finish_callback = &unpack_finish_callback;
	(*unpack)->thread.threads = glc_threads_hint(glc);

#ifdef __LZO
	lzo_init();
#endif

	return 0;
}

int unpack_process_start(unpack_t unpack, ps_buffer_t *from, ps_buffer_t *to)
{
	int ret;
	if (unpack->running)
		return EAGAIN;

	if ((ret = glc_thread_create(unpack->glc, &unpack->thread, from, to)))
		return ret;
	unpack->running = 1;

	return 0;
}

int unpack_process_wait(unpack_t unpack)
{
	if (!unpack->running)
		return EAGAIN;

	glc_thread_wait(&unpack->thread);
	unpack->running = 0;

	return 0;
}

int unpack_destroy(unpack_t unpack)
{
	free(unpack);
	return 0;
}

void unpack_finish_callback(void *ptr, int err)
{
	unpack_t unpack = (unpack_t) ptr;

	if (err)
		glc_log(unpack->glc, GLC_ERROR, "unpack", "%s (%d)", strerror(err), err);
}

int unpack_read_callback(glc_thread_state_t *state)
{
	if (state->header.type == GLC_MESSAGE_LZO) {
#ifdef __LZO
		state->write_size = ((glc_lzo_header_t *) state->read_data)->size;
		return 0;
#else
		glc_log(((unpack_t) state->ptr)->glc,
			 GLC_ERROR, "unpack", "LZO not supported");
		return ENOTSUP;
#endif
	} else if (state->header.type == GLC_MESSAGE_QUICKLZ) {
#ifdef __QUICKLZ
		state->write_size = ((glc_quicklz_header_t *) state->read_data)->size;
		return 0;
#else
		glc_log(((unpack_t) state->ptr)->glc,
			 GLC_ERROR, "unpack", "QuickLZ not supported");
		return ENOTSUP;
#endif
	} else if (state->header.type == GLC_MESSAGE_LZJB) {
#ifdef __LZJB
		state->write_size = ((glc_lzjb_header_t *) state->read_data)->size;
		return 0;
#else
		glc_log(((unpack_t) state->ptr)->glc,
			GLC_ERROR, "unpack", "LZJB not supported");
		return ENOTSUP;
#endif
	}

	state->flags |= GLC_THREAD_COPY;
	return 0;
}

int unpack_write_callback(glc_thread_state_t *state)
{
	if (state->header.type == GLC_MESSAGE_LZO) {
#ifdef __LZO
		memcpy(&state->header, &((glc_lzo_header_t *) state->read_data)->header,
		       sizeof(glc_message_header_t));
		__lzo_decompress((unsigned char *) &state->read_data[sizeof(glc_lzo_header_t)],
				state->read_size - sizeof(glc_lzo_header_t),
				(unsigned char *) state->write_data,
				(lzo_uintp) &state->write_size,
				NULL);
#else
		return ENOTSUP;
#endif
	} else if (state->header.type == GLC_MESSAGE_QUICKLZ) {
#ifdef __QUICKLZ
		memcpy(&state->header, &((glc_quicklz_header_t *) state->read_data)->header,
		       sizeof(glc_message_header_t));
		quicklz_decompress((const unsigned char *) &state->read_data[sizeof(glc_quicklz_header_t)],
				   (unsigned char *) state->write_data,
				   state->write_size);
#else
		return ENOTSUP;
#endif
	} else if (state->header.type == GLC_MESSAGE_LZJB) {
#ifdef __LZJB
		memcpy(&state->header, &((glc_quicklz_header_t *) state->read_data)->header,
		       sizeof(glc_message_header_t));
		lzjb_decompress(&state->read_data[sizeof(glc_lzjb_header_t)],
				state->write_data,
				state->read_size - sizeof(glc_lzjb_header_t),
				state->write_size);
#else
		return ENOTSUP;
#endif
	} else
		return ENOTSUP;

	return 0;
}

/**  \} */
