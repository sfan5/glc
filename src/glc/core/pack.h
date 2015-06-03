/**
 * \file glc/core/pack.h
 * \brief stream compression
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in glc.h
 */

/**
 * \addtogroup core
 *  \{
 * \defgroup pack stream compression
 *  \{
 */

#ifndef _PACK_H
#define _PACK_H

#include <packetstream.h>
#include <glc/common/glc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief pack object
 */
typedef struct pack_s* pack_t;

/** QuickLZ compression */
#define PACK_QUICKLZ       0x1
/** LZO compression */
#define PACK_LZO           0x2
/** LZJB compression */
#define PACK_LZJB          0x3

/**
 * \brief unpack object
 */
typedef struct unpack_s* unpack_t;

/**
 * \brief initialize pack object
 * \param pack pack object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int pack_init(pack_t *pack, glc_t *glc);

/**
 * \brief set compression
 *
 * QuickLZ (PACK_QUICKLZ) and LZO (PACK_LZO) are currently supported.
 * Both are fast enough for stream compression.
 * LZO compresses marginally better but is slower. QuickLZ is default.
 * \param pack pack object
 * \param compression compression algorithm
 * \return 0 on success otherwise an error code
 */
__PUBLIC int pack_set_compression(pack_t pack, int compression);

/**
 * \brief set compression threshold
 *
 * Packets smaller than compression threshold won't be compressed.
 * Default threshold is 1024.
 * \param pack pack object
 * \param min_size minimum packet size
 * \return 0 on success otherwise an error code
 */
__PUBLIC int pack_set_minimum_size(pack_t pack, ssize_t min_size);

/**
 * \brief start processing threads
 *
 * pack compresses all data that is practical to compress (currently
 * pictures and audio data) and wraps compressed data into container
 * packets.
 * \param pack pack object
 * \param from source buffer
 * \param to target buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int pack_process_start(pack_t pack, ps_buffer_t *from, ps_buffer_t *to);

/**
 * \brief block until process has finished
 * \param pack pack object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int pack_process_wait(pack_t pack);

/**
 * \brief destroy pack object
 * \param pack pack object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int pack_destroy(pack_t pack);


/**
 * \brief initialize unpack object
 * \param unpack unpack object
 * \param glc glc
 * \return 0 on success otherwise an error code
 */
__PUBLIC int unpack_init(unpack_t *unpack, glc_t *glc);

/**
 * \brief start processing threads
 *
 * unpack decompresses all supported compressed messages.
 * \param unpack unpack object
 * \param from source buffer
 * \param to target buffer
 * \return 0 on success otherwise an error code
 */
__PUBLIC int unpack_process_start(unpack_t unpack, ps_buffer_t *from,
				  ps_buffer_t *to);

/**
 * \brief block until process has finished
 * \param unpack unpack object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int unpack_process_wait(unpack_t unpack);

/**
 * \brief destroy unpack object
 * \param unpack unpack object
 * \return 0 on success otherwise an error code
 */
__PUBLIC int unpack_destroy(unpack_t unpack);

#ifdef __cplusplus
}
#endif

#endif

/**  \} */
/**  \} */
