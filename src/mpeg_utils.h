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
 * C. Moral rights of author belong to maki. Copyright is abandoned.
 *
 * D. Above three clauses are applied both to source and binary
 *   form of this software.
 *
 ****************************************************************************/
#ifndef __MPEG_UTILS_H__
#define __MPEG_UTILS_H__

#include "common.h"

#include "mpeg_common.h"

typedef struct {
    int64_t                 file_position;
    uint32_t                sample_size;
    uint32_t                raw_data_size;
    uint32_t                au_size;
    int64_t                 pcr;
    /* video. */
    int64_t                 video_pts;
    int64_t                 video_dts;
    uint16_t                video_program_id;
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
    uint16_t                audio_program_id;
    uint32_t                sampling_frequency;
    uint32_t                bitrate;
    uint16_t                channel;
    uint8_t                 layer;
    uint8_t                 bit_depth;
} stream_info_t;

#ifdef __cplusplus
extern "C" {
#endif

MAPI_EXPORT int mpeg_api_create_sample_list( void *ih );

MAPI_EXPORT int64_t mpeg_api_get_sample_position( void *ih, mpeg_sample_type sample_type, uint8_t stream_number );

MAPI_EXPORT int mpeg_api_set_sample_position
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    int64_t                     position
);

MAPI_EXPORT int mpeg_api_get_stream_data
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    uint8_t                   **dst_buffer,
    uint32_t                   *dst_read_size,
    get_sample_data_mode        get_mode
);

MAPI_EXPORT int mpeg_api_get_all_stream_data
(
    void                       *ih,
    get_sample_data_mode        get_mode,
    output_stream_type          output_stream,
    int                         update_psi,
    get_stream_data_cb_t       *cb
);

MAPI_EXPORT int mpeg_api_get_stream_all
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    get_sample_data_mode        get_mode,
    get_stream_data_cb_t       *cb
);

MAPI_EXPORT uint8_t mpeg_api_get_stream_num( void *ih, mpeg_sample_type sample_type, uint16_t service_id );

MAPI_EXPORT const char *mpeg_api_get_stream_information
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    get_information_key_type    key
);

MAPI_EXPORT const char *mpeg_api_get_sample_file_extension
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number
);

MAPI_EXPORT mpeg_stream_type mpeg_api_get_sample_stream_type
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number
);

MAPI_EXPORT uint32_t mpeg_api_get_sample_num
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number
);

MAPI_EXPORT int mpeg_api_get_sample_info
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    uint32_t                    sample_number,
    stream_info_t              *stream_info
);

MAPI_EXPORT int mpeg_api_get_sample_data
(
    void                       *ih,
    mpeg_sample_type            sample_type,
    uint8_t                     stream_number,
    uint32_t                    sample_number,
    uint8_t                   **dst_buffer,
    uint32_t                   *dst_read_size,
    get_sample_data_mode        get_mode
);

MAPI_EXPORT int mpeg_api_free_sample_buffer( void *ih, uint8_t **buffer );

MAPI_EXPORT int mpeg_api_get_pcr( void *ih, pcr_info_t *pcr_info, uint16_t service_id );

MAPI_EXPORT int mpeg_api_get_video_frame( void *ih, uint8_t stream_number, stream_info_t *stream_info );

MAPI_EXPORT int mpeg_api_get_audio_frame( void *ih, uint8_t stream_number, stream_info_t *stream_info );

MAPI_EXPORT int mpeg_api_parse( void *ih );

MAPI_EXPORT int mpeg_api_get_stream_parse_info
(
    void                       *ih,
    stream_info_t              *stream_info,
    int64_t                    *video_1st_pts,
    int64_t                    *video_key_pts,
    uint16_t                    service_id
);

MAPI_EXPORT int mpeg_api_set_pmt_target( void *ih, pmt_target_type pmt_target );

MAPI_EXPORT int mpeg_api_set_pmt_program_id( void *ih, uint16_t pmt_program_id );

MAPI_EXPORT int mpeg_api_set_service_id( void *ih, uint16_t service_id );

MAPI_EXPORT int mpeg_api_get_service_id_num( void *ih );

MAPI_EXPORT int mpeg_api_set_service_id_info( void *ih, service_id_info_t *sid_info, int32_t sid_info_num );

MAPI_EXPORT int mpeg_api_get_service_id_info( void *ih, service_id_info_t *sid_info, int32_t sid_info_num );

MAPI_EXPORT uint16_t mpeg_api_get_program_id( void *ih, mpeg_sample_type sample_type, uint8_t stream_number, uint16_t service_id );

MAPI_EXPORT void *mpeg_api_initialize_info( const char *mpeg, int64_t buffer_size );

MAPI_EXPORT void mpeg_api_release_info( void *ih );

MAPI_EXPORT void mpeg_api_setup_log_lv( log_level level, FILE *output );

#ifdef __cplusplus
}
#endif

#endif /* __MPEG_UTILS_H__ */
