/*****************************************************************************
 * mpeg_stream.h
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
#ifndef __MPEG_STREAM_H__
#define __MPEG_STREAM_H__

#include "mpeg_common.h"

#define PES_PACKET_START_CODE_SIZE                  (4)
#define PES_PACKET_HEADER_CHECK_SIZE                (5)
#define PES_PACKET_PTS_DTS_DATA_SIZE                (10)

typedef enum {
    /* MPEG-1/2 stream type. */
    PES_PACKET_START_CODE_PRIVATE_STREAM_1             ,
    PES_PACKET_START_CODE_PADDING_STREAM               ,
    PES_PACKET_START_CODE_PRIVATE_STREAM_2             ,
    PES_PACKET_START_CODE_VIDEO_STREAM                 ,
    PES_PACKET_START_CODE_AUDIO_STREAM                 ,
    PES_PACKET_START_CODE_MHEG_RESRVED                 ,
    /* MPEG-2 stream type. */
    PES_PACKET_START_CODE_PROGRAM_STERAM_MAP           ,
    PES_PACKET_START_CODE_LICENSE_MANAGEMENT_MAMESSAGE1,
    PES_PACKET_START_CODE_LICENSE_MANAGEMENT_MAMESSAGE2,
    PES_PACKET_START_CODE_DSM_CONTROL_COMAND           ,
    PES_PACKET_START_CODE_ITU_T_RESERVED1              ,
    PES_PACKET_START_CODE_ITU_T_RESERVED2              ,
    PES_PACKET_START_CODE_PS_TRANSPORT_ON_TS           ,
    PES_PACKET_START_CODE_PROGRAM_STREAM_DIRECTORY     ,
    /* User Private */
    PES_PACKET_START_CODE_MPEG4_AVC_STREAM             ,
    PES_PACKET_START_CODE_VC1_VIDEO_STREAM_1           ,
    PES_PACKET_START_CODE_VC1_VIDEO_STREAM_2           ,
    PES_PACKET_START_CODE_AC3_DTS_AUDIO_STREAM         ,
    PES_PACKET_START_CODE_MAX                          ,
    PES_PACKET_START_CODE_INVALID = PES_PACKET_START_CODE_MAX
} mpeg_pes_packet_start_code_type;

typedef struct {
    uint16_t        packet_length;
    /* reserved '10'    2bit */
    uint8_t         scrambe_control;
    uint8_t         priority;
    uint8_t         data_alignment;
    uint8_t         copyright;
    uint8_t         original_or_copy;
    uint8_t         pts_flag;
    uint8_t         dts_flag;
    uint8_t         escr_flag;
    uint8_t         es_rate_flag;
    uint8_t         dsm_trick_mode_flag;
    uint8_t         additional_copy_info_flag;
    uint8_t         crc_flag;
    uint8_t         extension_flag;
    uint8_t         header_length;
} mpeg_pes_header_info_t;

typedef enum {
    /* 0-1    : Reserved                                        */
    video_stream_descriptor                 = 0x02,
    audio_stream_descriptor                 = 0x03,
    hierarchy_descriptor                    = 0x04,
    registration_descriptor                 = 0x05,
    data_stream_alignment_descriptor        = 0x06,
    target_background_grid_descriptor       = 0x07,
    video_window_descriptor                 = 0x08,
    conditional_access_descriptor           = 0x09,
    ISO_639_language_descriptor             = 0x0A,
    system_clock_descriptor                 = 0x0B,
    multiplex_buffer_utilization_descriptor = 0x0C,
    copyright_descriptor                    = 0x0D,
    maximum_bitrate_descriptor              = 0x0E,
    private_data_indicator_descriptor       = 0x0F,
    smoothing_buffer_descriptor             = 0x10,
    STD_descriptor                          = 0x11,
    ibp_descriptor                          = 0x12,
    /* 19-26  : Defined in ISO/IEC 13818-6                      */
    MPEG4_video_descriptor                  = 0x1B,
    MPEG4_audio_descriptor                  = 0x1C,
    IOD_descriptor                          = 0x1D,
    SL_descriptor                           = 0x1E,
    FMC_descriptor                          = 0x1F,
    External_ES_ID_descriptor               = 0x20,
    MuxCode_descriptor                      = 0x21,
    FmxBufferSize_descriptor                = 0x22,
    MultiplexBuffer_descriptor              = 0x23,
    /* 36-63  : ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved   */
    /* 64-255 : User Private                                    */
    DESCRIPTOR_TAG_MAX_VALUE                = 0xFF
} mpeg_descriptor_tag_type;

#define DESCRIPTOR_INFO( name )         name##_descriptor_info_t

typedef struct {
    uint8_t         multiple_frame_rate_flag;
    uint8_t         frame_rate_code;
    uint8_t         MPEG_1_only_flag;
    uint8_t         constrained_parameter_flag;
    uint8_t         still_picture_flag;
    uint8_t         profile_and_level_indication;
    uint8_t         chroma_format;
    uint8_t         frame_rate_extension_flag;
} DESCRIPTOR_INFO( video_stream );

typedef struct {
    uint8_t         free_format_flag;
    uint8_t         id;
    uint8_t         layer;
    uint8_t         variable_rate_audio_indicator;
} DESCRIPTOR_INFO( audio_stream );

typedef struct {
    uint8_t         hierarchy_type;
    uint8_t         hierarchy_layer_index;
    uint8_t         hierarchy_embedded_layer_index;
    uint8_t         hierarchy_channel;
} DESCRIPTOR_INFO( hierarchy );

typedef struct {
    uint32_t        format_identifier;
    uint8_t        *additional_identification_info;         // FIXME
} DESCRIPTOR_INFO( registration );

typedef struct {
    uint8_t         alignment_type;
} DESCRIPTOR_INFO( data_stream_alignment );

typedef struct {
    uint16_t        horizontal_size;
    uint16_t        vertical_size;
    uint8_t         aspect_ratio_information;
} DESCRIPTOR_INFO( target_background_grid );

typedef struct {
    uint16_t        horizontal_offset;
    uint16_t        vertical_offset;
    uint8_t         window_priority;
} DESCRIPTOR_INFO( video_window );

typedef struct {
    uint16_t        CA_system_ID;
    uint16_t        CA_PID;
} DESCRIPTOR_INFO( conditional_access );

typedef struct {
    uint8_t         data_num;
    struct {
        uint32_t    ISO_639_language_code;
        uint8_t     audio_type;
    } data[256];
} DESCRIPTOR_INFO( ISO_639_language );

typedef struct {
    uint8_t         external_clock_reference_indicator;
    uint8_t         clock_accuracy_integer;
    uint8_t         clock_accuracy_exponent;
} DESCRIPTOR_INFO( system_clock );

typedef struct {
    uint8_t         bound_valid_flag;
    uint16_t        LTW_offset_lower_bound;
    uint16_t        LTW_offset_upper_bound;
} DESCRIPTOR_INFO( multiplex_buffer_utilization );

typedef struct {
    uint8_t         copyright_identifier;
} DESCRIPTOR_INFO( copyright );

typedef struct {
    uint32_t        maximum_bitrate;
} DESCRIPTOR_INFO( maximum_bitrate );

typedef struct {
    uint32_t        private_data_indicator;
} DESCRIPTOR_INFO( private_data_indicator );

typedef struct {
    uint32_t        sb_leak_rate;
    uint32_t        sb_size;
} DESCRIPTOR_INFO( smoothing_buffer );

typedef struct {
    uint8_t         leak_valid_flag;
} DESCRIPTOR_INFO( STD );

typedef struct {
    uint8_t         closed_gop_flag;
    uint8_t         identical_gop_flag;
    uint16_t        max_gop_length;
} DESCRIPTOR_INFO( ibp );

typedef struct {
    uint8_t         MPEG4_visual_profile_and_level;
} DESCRIPTOR_INFO( MPEG4_video );

typedef struct {
    uint8_t         MPEG4_audio_profile_and_level;
} DESCRIPTOR_INFO( MPEG4_audio );

typedef struct {
    uint8_t         Scope_of_IOD_label;
    uint8_t         IOD_label;
    uint8_t         InitialObjectDescriptor;        // FIXME
} DESCRIPTOR_INFO( IOD );

typedef struct {
    uint8_t         ES_ID;
} DESCRIPTOR_INFO( SL );

typedef struct {
    uint8_t         data_num;
    struct {
        uint16_t    ES_ID;
        uint8_t     FlexMuxChannel;
    } data[86];         /* 86: 256(uint8_t MAX) / 3 */
} DESCRIPTOR_INFO( FMC );

typedef struct {
    uint16_t        External_ES_ID;
} DESCRIPTOR_INFO( External_ES_ID );

typedef struct {
    uint8_t        *MuxCodeTableEntry;      // FIXME
} DESCRIPTOR_INFO( MuxCode );

typedef struct {
    uint8_t        *DefaultFlexMuxBufferDescriptor;      // FIXME
    uint8_t        *FlexMuxBufferDescriptor;             // FIXME
} DESCRIPTOR_INFO( FmxBufferSize );

typedef struct {
    uint32_t        MB_buffer_size;
    uint32_t        TB_leak_rate;
} DESCRIPTOR_INFO( MultiplexBuffer );

#define STRUCT_DESCRIPTOR( name )       DESCRIPTOR_INFO( name )     name;
typedef struct {
    mpeg_descriptor_tag_type                    tag;
    uint8_t                                     length;
    STRUCT_DESCRIPTOR( video_stream )
    STRUCT_DESCRIPTOR( audio_stream )
    STRUCT_DESCRIPTOR( hierarchy )
    STRUCT_DESCRIPTOR( registration )
    STRUCT_DESCRIPTOR( data_stream_alignment )
    STRUCT_DESCRIPTOR( target_background_grid )
    STRUCT_DESCRIPTOR( video_window )
    STRUCT_DESCRIPTOR( conditional_access )
    STRUCT_DESCRIPTOR( ISO_639_language )
    STRUCT_DESCRIPTOR( system_clock )
    STRUCT_DESCRIPTOR( multiplex_buffer_utilization )
    STRUCT_DESCRIPTOR( copyright )
    STRUCT_DESCRIPTOR( maximum_bitrate )
    STRUCT_DESCRIPTOR( private_data_indicator )
    STRUCT_DESCRIPTOR( smoothing_buffer )
    STRUCT_DESCRIPTOR( STD )
    STRUCT_DESCRIPTOR( ibp )
    STRUCT_DESCRIPTOR( MPEG4_video )
    STRUCT_DESCRIPTOR( MPEG4_audio )
    STRUCT_DESCRIPTOR( IOD )
    STRUCT_DESCRIPTOR( SL )
    STRUCT_DESCRIPTOR( FMC )
    STRUCT_DESCRIPTOR( External_ES_ID )
    STRUCT_DESCRIPTOR( MuxCode )
    STRUCT_DESCRIPTOR( FmxBufferSize )
    STRUCT_DESCRIPTOR( MultiplexBuffer )
} mpeg_descriptor_info_t;
#undef STRUCT_DESCRIPTOR

#undef DESCRIPTOR_INFO

#define STREAM_MPA_HEADER_CHECK_SIZE                    (4)
#define STREAM_AAC_HEADER_CHECK_SIZE                    (7)
#define STREAM_LPCM_HEADER_CHECK_SIZE                   (4)
#define STREAM_AC3_HEADER_CHECK_SIZE                    (8)
#define STREAM_EAC3_HEADER_CHECK_SIZE                   (12)
#define STREAM_DTS_HEADER_CHECK_SIZE                    (15)

#define STREAM_HEADER_CHECK_MAX_SIZE                    STREAM_DTS_HEADER_CHECK_SIZE

typedef struct {
    uint32_t            frame_length;
    /* audio. */
    uint32_t            sampling_frequency;
    uint32_t            bitrate;
    uint8_t             channel;
    uint8_t             layer;
    uint8_t             bit_depth;
} mpeg_stream_raw_info_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int64_t mpeg_pes_get_timestamp( uint8_t *time_stamp_data );

extern int mpeg_pes_check_start_code( uint8_t *start_code, mpeg_pes_packet_start_code_type start_code_type );

extern void mpeg_pes_get_header_info( uint8_t *buf, mpeg_pes_header_info_t *pes_info );

extern mpeg_pes_packet_start_code_type mpeg_pes_get_stream_start_code( mpeg_stream_group_type stream_judge );

extern void mpeg_stream_get_descriptor_info
(
 /* mpeg_stream_type            stream_type, */
    uint8_t                    *descriptor,
    mpeg_descriptor_info_t     *descriptor_info
);

extern void mpeg_stream_debug_descriptor_info( mpeg_descriptor_info_t *descriptor_info );

extern mpeg_stream_group_type mpeg_stream_judge_type
(
    mpeg_stream_type            stream_type,
    uint8_t                    *descriptor_tags,
    uint16_t                    descriptor_num
);

extern int32_t mpeg_stream_check_header
(
    mpeg_stream_type            stream_type,
    mpeg_stream_group_type      stream_judge,
    int                         search_point,
    uint8_t                    *buffer,
    uint32_t                    buffer_size,
    mpeg_stream_raw_info_t     *stream_raw_info,
    int32_t                    *data_offset
);

extern int mpeg_stream_check_header_skip( mpeg_stream_group_type stream_judge );

extern uint32_t mpeg_stream_get_header_check_size( mpeg_stream_type stream_type, mpeg_stream_group_type stream_judge );

#ifdef __cplusplus
}
#endif

#endif /* __MPEG_STREAM_H__ */
