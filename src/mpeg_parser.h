/*****************************************************************************
 * mpeg_parser.h
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
#ifndef __MPEG_PARSER_H__
#define __MPEG_PARSER_H__

#include "mpeg_common.h"

typedef enum {
    PARSER_STATUS_NON_PARSING = 0,
    PARSER_STATUS_PARSED
} parser_status_type;

typedef enum {
    PID_TYPE_PAT,
    PID_TYPE_PMT,
    PID_TYPE_VIDEO,
    PID_TYPE_AUDIO
} mpegts_select_pid_type;

typedef struct {
    int64_t                 file_position;
    uint32_t                sample_size;
    uint32_t                raw_data_size;
    int32_t                 raw_data_read_offset;
    int64_t                 pts;
    int64_t                 dts;
    int64_t                 gop_number;
    uint8_t                 progressive_sequence;
    uint8_t                 closed_gop;
    uint8_t                 picture_coding_type;
    int16_t                 temporal_reference;
    uint8_t                 progressive_frame;
    uint8_t                 picture_structure;
    uint8_t                 repeat_first_field;
    uint8_t                 top_field_first;
} video_sample_info_t;

typedef struct {
    int64_t                 file_position;
    uint32_t                sample_size;
    uint32_t                raw_data_size;
    int32_t                 raw_data_read_offset;
    int64_t                 pts;
    int64_t                 dts;
    uint32_t                sampling_frequency;
    uint32_t                bitrate;
    uint8_t                 channel;
    uint8_t                 layer;
    uint8_t                 bit_depth;
} audio_sample_info_t;

typedef struct {
    void *              (* initialize               )( const char *input, int64_t buffer_size );
    void                (* release                  )( void *ih );
    int                 (* parse                    )( void *ih );
    int                 (* set_program_target       )( void *ih, pmt_target_type pmt_target );
    int                 (* set_program_id           )( void *ih, mpegts_select_pid_type pid_type, uint16_t program_id );
    uint16_t            (* get_program_id           )( void *ih, mpeg_stream_type stream_type );
    int                 (* get_video_info           )( void *ih, uint8_t stream_number, video_sample_info_t *video_info );
    int                 (* get_audio_info           )( void *ih, uint8_t stream_number, audio_sample_info_t *audio_info );
    int                 (* get_pcr                  )( void *ih, pcr_info_t *pcr_info );
    uint8_t             (* get_stream_num           )( void *ih, mpeg_sample_type sample_type );
    int                 (* get_stream_data          )( void *ih, mpeg_sample_type sample_type, uint8_t stream_number, int32_t read_offset, get_sample_data_mode get_mode, get_stream_data_cb_t *cb );
    int                 (* get_specific_stream_data )( void *ih, get_sample_data_mode get_mode, output_stream_type output_stream, get_stream_data_cb_t *cb );
    int64_t             (* get_sample_position      )( void *ih, mpeg_sample_type sample_type, uint8_t stream_number );
    int                 (* set_sample_position      )( void *ih, mpeg_sample_type sample_type, uint8_t stream_number, int64_t position );
    int                 (* seek_next_sample_position)( void *ih, mpeg_sample_type sample_type, uint8_t stream_number );
    int                 (* get_sample_data          )( void *ih, mpeg_sample_type sample_type, uint8_t stream_number, int64_t position, uint32_t sample_size, int32_t read_offset, uint8_t **dst_buffer, uint32_t *dst_read_size, get_sample_data_mode get_mode );
    void                (* free_sample_buffer       )( uint8_t **buffer );
    mpeg_stream_type    (* get_sample_stream_type   )( void *ih, mpeg_sample_type sample_type, uint8_t stream_number );
    const char *        (* get_stream_information   )( void *ih, mpeg_sample_type sample_type, uint8_t stream_number, get_information_key_type key );
} mpeg_parser_t;

extern mpeg_parser_t mpeges_parser;
extern mpeg_parser_t mpegts_parser;

#define MPEG_PARSER_NUM     (2)

#endif /* __MPEG_PARSER_H__ */
