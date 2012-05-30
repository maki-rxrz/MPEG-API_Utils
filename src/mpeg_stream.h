/*****************************************************************************
 * mpeg_stream.h
 *****************************************************************************
 *
 * Copyright (C) 2012 maki
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
 *   A-4. When you release a modified version to public, you
 *       must publish it with your name.
 *
 * B. The author is not responsible for any kind of damages or loss
 *   while using or misusing this software, which is distributed
 *   "AS IS". No warranty of any kind is expressed or implied.
 *   You use AT YOUR OWN RISK.
 *
 * C. Copyrighted to maki.
 *
 * D. Above three clauses are applied both to source and binary
 *   form of this software.
 *
 ****************************************************************************/
#ifndef __MPEG_STREAM_H__
#define __MPEG_STREAM_H__

#include <inttypes.h>

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
    /* ... */
    PES_PACKET_START_CODE_MPEG4_AVC_STREAM             ,
    PES_PACKET_START_CODE_VC1_VIDEO_STREAM_1           ,
    PES_PACKET_START_CODE_VC1_VIDEO_STREAM_2           ,
    PES_PACKET_START_CODE_AC3_DTS_AUDIO_STREAM         ,
    PES_PACKET_START_CODE_MAX
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
    uint8_t         extention_flag;
    uint8_t         header_length;
} mpeg_pes_header_info_t;

#define MPEG_VIDEO_START_CODE_SIZE                      (4)

#define MPEG_VIDEO_SEQUENCE_SECTION_HEADER_SIZE         (136)
#define MPEG_VIDEO_GOP_SECTION_HEADER_SIZE              (4)
#define MPEG_VIDEO_PICTURE_SECTION_HEADER_SIZE          (5)
#define MPEG_VIDEO_SLICE_SECTION_HEADER_SIZE            (2)
//#define MPEG_VIDEO_MACROBLOCK_SECTION_HEADER_SIZE       (0)

#define MPEG_VIDEO_SEQUENCE_EXTENSION_SIZE                      (6)
#define MPEG_VIDEO_SEQUENCE_DISPLAY_EXTENSION_SIZE              (8)
#define MPEG_VIDEO_SEQUENCE_SCALABLE_EXTENSION_SIZE             (7)
#define MPEG_VIDEO_PICTURE_CODING_EXTENSION_SIZE                (7)
#define MPEG_VIDEO_QUANT_MATRIX_EXTENSION_SIZE                  (257)
#define MPEG_VIDEO_PICTURE_DISPLAY_EXTENSION_SIZE               (14)
#define MPEG_VIDEO_PICTURE_TEMPORAL_SCALABLE_EXTENSION_SIZE     (4)
#define MPEG_VIDEO_PICTURE_SPATIAL_SCALABLE_EXTENSION_SIZE      (7)
#define MPEG_VIDEO_COPYRIGHT_EXTENSION_SIZE                     (11)

#define MPEG_VIDEO_HEADER_EXTENSION_MIN_SIZE            MPEG_VIDEO_PICTURE_TEMPORAL_SCALABLE_EXTENSION_SIZE
#define MPEG_VIDEO_HEADER_EXTENSION_MAX_SIZE            MPEG_VIDEO_QUANT_MATRIX_EXTENSION_SIZE

typedef enum {
    MPEG_VIDEO_START_CODE_SHC  = 0,         /* Sequence Header Code */
    MPEG_VIDEO_START_CODE_ESC  = 1,         /* Extension Start Code */
    MPEG_VIDEO_START_CODE_UDSC = 2,         /* User Data Start Code */
    MPEG_VIDEO_START_CODE_SEC  = 3,         /* Sequence End Code    */
    MPEG_VIDEO_START_CODE_GSC  = 4,         /* Group Start Code     */
    MPEG_VIDEO_START_CODE_PSC  = 5,         /* Picture Start Code   */
    MPEG_VIDEO_START_CODE_SSC  = 6,         /* Slice Start Code     */
    MPEG_VIDEO_START_CODE_MAX
} mpeg_video_start_code_type;

typedef struct {
    uint16_t        horizontal_size;
    uint16_t        vertical_size;
    uint8_t         aspect_ratio_information;
    uint8_t         frame_rate_code;
    uint32_t        bit_rate;
    uint16_t        vbv_buffer_size;
    uint8_t         constrained_parameters_flag;
    uint8_t         load_intra_quantiser_matrix;
    uint8_t         intra_quantiser_matrix[64];
    uint8_t         load_non_intra_quantiser_matrix;
    uint8_t         non_intra_quantiser_matrix[64];
} mpeg_video_sequence_header_t;

typedef struct {
    uint8_t         profile_and_level_indication;
    uint8_t         progressive_sequence;
    uint8_t         chroma_format;
    uint8_t         horizontal_size_extension;
    uint8_t         vertical_size_extension;
    uint16_t        bit_rate_extension;
    uint8_t         vbv_buffer_size_extension;
    uint8_t         low_delay;
    uint8_t         frame_rate_extension_n;
    uint8_t         frame_rate_extension_d;
} mpeg_video_sequence_extension_t;

typedef struct {
    uint8_t         video_format;
    uint8_t         colour_description;
    uint8_t         colour_primaries;
    uint8_t         transfer_characteristics;
    uint8_t         matrix_coefficients;
    uint16_t        display_horizontal_size;
    uint16_t        display_vertical_size;
} mpeg_video_sequence_display_extension_t;

typedef struct {
    uint8_t         scalable_mode;
    uint8_t         layer_id;
    uint16_t        lower_layer_prediction_horizontal_size;
    uint16_t        lower_layer_prediction_vertical_size;
    uint8_t         horizontal_subsampling_factor_m;
    uint8_t         horizontal_subsampling_factor_n;
    uint8_t         vertical_subsampling_factor_m;
    uint8_t         vertical_subsampling_factor_n;
    uint8_t         picture_mux_enable;
    uint8_t         mux_to_progressive_sequence;
    uint8_t         picture_mux_order;
    uint8_t         picture_mux_factor;
} mpeg_video_sequence_scalable_extension_t;

typedef struct {
    uint32_t        time_code;
    uint8_t         closed_gop;
    uint8_t         broken_link;
} mpeg_video_gop_header_t;

typedef struct {
    uint16_t        temporal_reference;
    uint8_t         picture_coding_type;
    uint16_t        vbv_delay;
    uint8_t         full_pel_forward_vector;
    uint8_t         forward_f_code;
    uint8_t         full_pel_backword_vector;
    uint8_t         backward_f_code;
/*  uint8_t         extra_information_picture[?] */     /* reserved */
} mpeg_video_picture_header_t;

typedef struct {
    struct {
        uint8_t     horizontal;
        uint8_t     vertical;
    } f_code[2];    /* forward / backward */
    uint8_t         intra_dc_precision;
    uint8_t         picture_structure;
    uint8_t         top_field_first;
    uint8_t         frame_predictive_frame_dct;
    uint8_t         concealment_motion_vectors;
    uint8_t         q_scale_type;
    uint8_t         intra_vlc_format;
    uint8_t         alternate_scan;
    uint8_t         repeat_first_field;
    uint8_t         chroma_420_type;
    uint8_t         progressive_frame;
    uint8_t         composite_display_flag;
    uint8_t         v_axis;
    uint8_t         field_sequence;
    uint8_t         sub_carrier;
    uint8_t         burst_amplitude;
    uint8_t         sub_carrier_phase;
} mpeg_video_picture_coding_extension_t;

typedef struct {
    uint8_t         load_intra_quantiser_matrix;
    uint8_t         intra_quantiser_matrix[64];
    uint8_t         load_non_intra_quantiser_matrix;
    uint8_t         non_intra_quantiser_matrix[64];
    uint8_t         load_chroma_intra_quantiser_matrix;
    uint8_t         chroma_intra_quantiser_matrix[64];
    uint8_t         load_chroma_non_intra_quantiser_matrix;
    uint8_t         chroma_non_intra_quantiser_matrix[64];
} mpeg_video_quant_matrix_extension_t;

typedef struct {
    uint8_t         number_of_frame_centre_offsets;
    struct {
        uint16_t    horizontal_offset;
        uint16_t    vertical_offset;
    } frame_centre_offsets[3];
} mpeg_video_picture_display_extension_t;

typedef struct {
    uint8_t         reference_select_code;
    uint16_t        forward_temporal_reference;
    uint16_t        backward_temporal_reference;
} mpeg_video_picture_temporal_scalable_extension_t;

typedef struct {
    uint16_t        lower_layer_temporal_reference;
    int16_t         lower_layer_horizontal_offset;
    int16_t         lower_layer_vertical_offset;
    uint8_t         spatial_temporal_weight_code_table_index;
    uint8_t         lower_layer_progressive_frame;
    uint8_t         lower_layer_deinterlaced_field_select;
} mpeg_video_picture_spatial_scalable_extension_t;

typedef struct {
    uint8_t         copyright_flag;
    uint8_t         copyright_identifier;
    uint8_t         original_or_copy;
    uint32_t        copyright_number_1;
    uint32_t        copyright_number_2;
    uint32_t        copyright_number_3;
} mpeg_video_copyright_extension_t;

typedef struct {
    uint8_t         slice_vertical_position_extension;
    uint8_t         priority_breakpoint;
    uint8_t         quantiser_scale_code;
    uint8_t         intra_slice_flag;
    uint8_t         intra_slice;
//    uint8_t         reserved_bits;
//    uint8_t         extra_bit_slice;
//    uint8_t         extra_information_slice;
} mpeg_video_slice_header_t;

#if 0
typedef struct {
    uint8_t         reference_select_code;
    uint16_t        forward_temporal_reference;
    uint16_t        backward_temporal_reference;
} mpeg_video_macroblock_header_t;
#endif

typedef struct {
    mpeg_video_sequence_header_t                        sequence;
    mpeg_video_sequence_extension_t                     sequence_ext;
    mpeg_video_sequence_display_extension_t             sequence_display_ext;
    mpeg_video_sequence_scalable_extension_t            sequence_scalable_ext;
    mpeg_video_gop_header_t                             gop;
    mpeg_video_picture_header_t                         picture;
    mpeg_video_picture_coding_extension_t               picture_coding_ext;
    mpeg_video_quant_matrix_extension_t                 quant_matrix_ext;
    mpeg_video_picture_display_extension_t              picture_display_ext;
    mpeg_video_picture_temporal_scalable_extension_t    picture_temporal_scalable_ext;
    mpeg_video_picture_spatial_scalable_extension_t     picture_spatial_scalable_ext;
    mpeg_video_copyright_extension_t                    copyright_ext;
    mpeg_video_slice_header_t                           slice;
#if 0
    mpeg_video_macroblock_header_t                      macroblock;
#endif
} mpeg_video_info_t;

typedef enum {
    NON_EXTENSTION                = 0,
    SEQUENCE_EXT                  = 1,      /* Sequence extension                   */
    SEQUENCE_DISPLAY_EXT          = 2,      /* Sequence display extension           */
    SEQUENCE_SCALABLE_EXT         = 3,      /* Sequence scalable extension          */
    PICTURE_CODING_EXT            = 4,      /* Picture coding extension             */
    QUANT_MATRIX_EXT              = 5,      /* Quant matrix extension               */
    PICTURE_DISPLAY_EXT           = 6,      /* Picture display extension            */
    PICTURE_TEMPORAL_SCALABLE_EXT = 7,      /* Picture temporal scalable extension  */
    PICTURE_SPATIAL_SCALABLE_EXT  = 8,      /* Picture spatial scalable extension   */
    COPYRIGHT_EXT                 = 9,      /* Copyright extension                  */
    EXTENSION_TYPE_MAX
} mpeg_video_extension_type;

typedef enum {
    NON_DETECT = -1,
    DETECT_SHC,
    DETECT_SESC,
    DETECT_SDE,
    DETECT_SSE,
    DETECT_ESC,
    DETECT_UDSC,
    DETECT_SEC,
    DETECT_GSC,
    DETECT_PSC,
    DETECT_PCESC,
    DETECT_QME,
    DETECT_PDE,
    DETECT_PTSE,
    DETECT_PSSE,
    DETECT_CPRE,
    DETECT_SSC
} mpeg_video_start_code_searching_status;

typedef struct {
    mpeg_video_start_code_type              start_code;
    uint32_t                                read_size;
    mpeg_video_start_code_searching_status  searching_status;
} mpeg_video_start_code_info_t;

#define PAL_FRAME_RATE_NUM      (25)
#define PAL_FRAME_RATE_DEN      (1)
#define NTSC_FRAME_RATE_NUM     (30000)
#define NTSC_FRAME_RATE_DEN     (1001)

#ifdef __cplusplus
extern "C" {
#endif

extern int64_t mpeg_pes_get_timestamp( uint8_t *time_stamp_data );

extern int mpeg_pes_check_start_code( uint8_t *start_code, mpeg_pes_packet_start_code_type start_code_type );

extern void mpeg_pes_get_header_info( uint8_t *buf, mpeg_pes_header_info_t *pes_info );

extern int mpeg_stream_judge_type( uint8_t stream_type );

extern int32_t mpeg_stream_check_start_point( mpeg_stream_type stream_type, uint8_t *buffer, uint32_t buffer_size );

extern int mpeg_video_check_start_code_common_head( uint8_t *start_code );

extern int mpeg_video_check_start_code( uint8_t *start_code, mpeg_video_start_code_type start_code_type );

extern int mpeg_video_judge_start_code( uint8_t *start_code_data, uint8_t identifier, mpeg_video_start_code_info_t *start_code_info );

extern int32_t mpeg_video_get_header_info( uint8_t *buf, mpeg_video_start_code_type start_code, mpeg_video_info_t *video_info );

extern void mpeg_video_debug_header_info( mpeg_video_info_t *video_info, mpeg_video_start_code_searching_status searching_status );

extern void mpeg_video_get_frame_rate( mpeg_video_info_t *video_info, uint32_t *fps_numerator, uint32_t *fps_denominator );

#ifdef __cplusplus
}
#endif

#endif /* __MPEG_STREAM_H__ */
