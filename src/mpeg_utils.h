/*****************************************************************************
 * mpeg_utils.h
 *****************************************************************************
 *
 * Authors: Masaki Tanaka <maki.rxrz@gmail.com>
 *
 * NYSL Version 0.9982 (en) (Unofficial)
 * ----------------------------------------
 * A. This software is "Everyone'sWare". It means:
 *   Anybody who has this software can use it as if he/she is
 *   the author.
 *
 *   A-1. Freeware. No fee is required.
 *   A-2. You can freely redistribute this software.
 *   A-3. You can freely modify this software. And the source
 *       may be used in any software with no limitation.
 *
 * B. The author is not responsible for any kind of damages or loss
 *   while using or misusing this software, which is distributed
 *   "AS IS". No warranty of any kind is expressed or implied.
 *   You use AT YOUR OWN RISK.
 *
 * C. Moral rights of author belong to maki. Copyright abandons.
 *
 * D. Above three clauses are applied both to source and binary
 *   form of this software.
 *
 ****************************************************************************/
#ifndef __MPEG_UTILS_H__
#define __MPEG_UTILS_H__

#include "common.h"

#include <inttypes.h>

#include "mpeg_common.h"

typedef struct {
    int64_t                 file_position;
    uint32_t                sample_size;
    uint32_t                raw_data_size;
    int64_t                 pcr;
    /* video. */
    int64_t                 video_pts;
    int64_t                 video_dts;
    int64_t                 gop_number;
    uint8_t                 progressive_sequence;
    uint8_t                 closed_gop;
    uint8_t                 picture_coding_type;
    int16_t                 temporal_reference;
    uint8_t                 picture_structure;
    uint8_t                 progressive_frame;
    uint8_t                 repeat_first_field;
    uint8_t                 top_field_first;
    /* audio. */
    int64_t                 audio_pts;
    int64_t                 audio_dts;
    uint32_t                sampling_frequency;
    uint32_t                bitrate;
    uint8_t                 channel;
    uint8_t                 layer;
    uint8_t                 bit_depth;
} stream_info_t;

#ifdef __cplusplus
extern "C" {
#endif

MAPI_EXPORT int mpeg_api_create_sample_list( void *ih );

MAPI_EXPORT uint8_t mpeg_api_get_stream_num( void *ih, mpeg_sample_type sample_type );

MAPI_EXPORT const char *mpeg_api_get_sample_file_extension( void *ih, mpeg_sample_type sample_type, uint8_t stream_number );

MAPI_EXPORT mpeg_stream_type mpeg_api_get_sample_stream_type( void *ih, mpeg_sample_type sample_type, uint8_t stream_number );

MAPI_EXPORT uint32_t mpeg_api_get_sample_num( void *ih, mpeg_sample_type sample_type, uint8_t stream_number );

MAPI_EXPORT int mpeg_api_get_sample_info( void *ih, mpeg_sample_type sample_type, uint8_t stream_number, uint32_t sample_number, stream_info_t *stream_info );

MAPI_EXPORT int mpeg_api_get_sample_data( void *ih, mpeg_sample_type sample_type, uint8_t stream_number, uint32_t sample_number, uint8_t **dst_buffer, uint32_t *dst_read_size, get_sample_data_mode get_mode );

MAPI_EXPORT int mpeg_api_free_sample_buffer( void *ih, uint8_t **buffer );

MAPI_EXPORT int64_t mpeg_api_get_pcr( void *ih );

MAPI_EXPORT int mpeg_api_get_video_frame( void *ih, uint8_t stream_number, stream_info_t *stream_info );

MAPI_EXPORT int mpeg_api_get_audio_frame( void *ih, uint8_t stream_number, stream_info_t *stream_info );

MAPI_EXPORT int mpeg_api_parse( void *ih );

MAPI_EXPORT int mpeg_api_get_stream_info( void *ih, stream_info_t *stream_info, int64_t *video_1st_pts, int64_t*video_key_pts );

MAPI_EXPORT int mpeg_api_set_pmt_program_id( void *ih, uint16_t pmt_program_id );

MAPI_EXPORT void *mpeg_api_initialize_info( const char *mpeg );

MAPI_EXPORT void mpeg_api_release_info( void *ih );

MAPI_EXPORT void mpeg_api_setup_log_lv( log_level level, FILE *output );

#ifdef __cplusplus
}
#endif

#endif /* __MPEG_UTILS_H__ */
