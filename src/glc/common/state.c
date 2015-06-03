/**
 * \file glc/common/state.c
 * \brief glc state
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup state
 *  \{
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "glc.h"
#include "core.h"
#include "log.h"
#include "state.h"

struct glc_state_video_s {
	glc_stream_id_t id;

	struct glc_state_video_s *next;
};

struct glc_state_audio_s {
	glc_stream_id_t id;

	struct glc_state_audio_s *next;
};

struct glc_state_s {
	pthread_rwlock_t state_rwlock;

	pthread_rwlock_t time_rwlock;
	glc_stime_t time_difference;

	pthread_rwlock_t video_rwlock;
	struct glc_state_video_s *video;
	glc_stream_id_t video_count;

	pthread_rwlock_t audio_rwlock;
	struct glc_state_audio_s *audio;
	glc_stream_id_t audio_count;
};

int glc_state_init(glc_t *glc)
{
	glc->state_flags = 0;
	glc->state = (glc_state_t) malloc(sizeof(struct glc_state_s));
	memset(glc->state, 0, sizeof(struct glc_state_s));

	pthread_rwlock_init(&glc->state->state_rwlock, NULL);
	pthread_rwlock_init(&glc->state->time_rwlock, NULL);

	pthread_rwlock_init(&glc->state->video_rwlock, NULL);
	pthread_rwlock_init(&glc->state->audio_rwlock, NULL);

	return 0;
}

int glc_state_destroy(glc_t *glc)
{
	struct glc_state_video_s *video_del;
	struct glc_state_audio_s *audio_del;

	while (glc->state->video != NULL) {
		video_del = glc->state->video;
		glc->state->video = glc->state->video->next;

		free(video_del);
	}

	while (glc->state->audio != NULL) {
		audio_del = glc->state->audio;
		glc->state->audio = glc->state->audio->next;

		free(audio_del);
	}

	pthread_rwlock_destroy(&glc->state->state_rwlock);
	pthread_rwlock_destroy(&glc->state->time_rwlock);

	pthread_rwlock_destroy(&glc->state->video_rwlock);
	pthread_rwlock_destroy(&glc->state->audio_rwlock);

	free(glc->state);
	glc->state_flags = 0;

	return 0;
}

int glc_state_video_new(glc_t *glc, glc_stream_id_t *id,
			glc_state_video_t *video)
{
	*video = (glc_state_video_t) malloc(sizeof(struct glc_state_video_s));
	memset(*video, 0, sizeof(struct glc_state_video_s));

	pthread_rwlock_wrlock(&glc->state->video_rwlock);
	(*video)->id = ++glc->state->video_count;
	(*video)->next = glc->state->video;
	glc->state->video = *video;
	pthread_rwlock_unlock(&glc->state->video_rwlock);

	*id = (*video)->id;
	return 0;
}

int glc_state_audio_new(glc_t *glc, glc_stream_id_t *id,
			glc_state_audio_t *audio)
{
	*audio = (glc_state_audio_t) malloc(sizeof(struct glc_state_audio_s));
	memset(*audio, 0, sizeof(struct glc_state_audio_s));

	pthread_rwlock_wrlock(&glc->state->audio_rwlock);
	(*audio)->id = ++glc->state->audio_count;
	(*audio)->next = glc->state->audio;
	glc->state->audio = *audio;
	pthread_rwlock_unlock(&glc->state->audio_rwlock);

	*id = (*audio)->id;
	return 0;
}

int glc_state_set(glc_t *glc, int flag)
{
	pthread_rwlock_wrlock(&glc->state->state_rwlock);
	glc->state_flags |= flag;
	pthread_rwlock_unlock(&glc->state->state_rwlock);
	return 0;
}

int glc_state_clear(glc_t *glc, int flag)
{
	pthread_rwlock_wrlock(&glc->state->state_rwlock);
	glc->state_flags &= ~flag;
	pthread_rwlock_unlock(&glc->state->state_rwlock);
	return 0;
}

// Implementation of glc_state_test is located in state.h

glc_utime_t glc_state_time(glc_t *glc)
{
	return glc_time(glc) - glc->state->time_difference;
}

int glc_state_time_add_diff(glc_t *glc, glc_stime_t diff)
{
	glc_log(glc, GLC_DEBUG, "state", "applying %ld usec time difference", diff);
	pthread_rwlock_wrlock(&glc->state->time_rwlock);
	glc->state->time_difference += diff;
	pthread_rwlock_unlock(&glc->state->time_rwlock);
	return 0;
}

/**  \} */
