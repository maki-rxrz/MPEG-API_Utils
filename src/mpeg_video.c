/*****************************************************************************
 * mpeg_video.c
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

#include "common.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "mpeg_common.h"
#include "mpeg_stream.h"

extern int mpeg_video_check_start_code( uint8_t *start_code, mpeg_video_star_code_type start_code_type )
{
    static const uint8_t video_start_code_common_head[MPEG_VIDEO_STATRT_CODE_SIZE - 1] = { 0x00, 0x00, 0x01 };
    static const uint8_t mpeg_video_start_code[MPEG_VIDEO_START_CODE_MAX + 1] =
        {
            /* Sequence Hedaer */
            0xB3,                   /* Sequence Hreader Code    */
            0xB5,                   /* Extension Start Code     */
            0xB2,                   /* User Data Start Code     */
            0xB7,                   /* Sequence End Code        */
            /* Picture Hedaer */
            0xB8,                   /* GOP Start Code           */
            0x00,                   /* Picture Start Code       */
            /* Slice Hedaer */
            0x01,                   /* Slice Start Code - Min   */
            0xAF                    /* Slice Start Code - Max   */
        };
    if( memcmp( start_code, video_start_code_common_head, MPEG_VIDEO_STATRT_CODE_SIZE - 1 ) )
        return -1;
    if( start_code_type == MPEG_VIDEO_START_CODE_SSC )
    {
        if( start_code[MPEG_VIDEO_STATRT_CODE_SIZE - 1] < mpeg_video_start_code[start_code_type + 0]
         || start_code[MPEG_VIDEO_STATRT_CODE_SIZE - 1] > mpeg_video_start_code[start_code_type + 1] )
            return -1;
    }
    else if( start_code[MPEG_VIDEO_STATRT_CODE_SIZE - 1] != mpeg_video_start_code[start_code_type] )
        return -1;
    return 0;
}

static void read_sequence_header( uint8_t *data, mpeg_video_sequence_header_t *sequence )
{
    sequence->horizontal_size             =   (data[0] << 4) | ((data[1] & 0xF0) >> 4);
    sequence->vertical_size               =  ((data[1] & 0x0F) << 8) | (data[2]);
    sequence->aspect_ratio_information    =   (data[3] & 0xF0) >> 4;
    sequence->frame_rate_code             =    data[3] & 0x0F;
    sequence->bit_rate                    =   (data[4] << 10) | (data[5] << 2) | ((data[6] & 0xC0) >> 6);
    /* marker_bit '1'                     = !!(data[6] & 0x20); */
    sequence->vbv_buffer_size             =  ((data[6] & 0x1F) << 5) | ((data[6] & 0xF8) >> 3);
    sequence->constrained_parameters_flag = !!(data[6] & 0x04);
    sequence->load_intra_quantiser_matrix = !!(data[6] & 0x02);
    uint8_t *p = &(data[6]);
    if( sequence->load_intra_quantiser_matrix )
        for( int i = 0; i < 64; ++i, ++p )
            sequence->intra_quantiser_matrix[i]     =   (*p & 0x01) << 7 | *(p+1) >> 1;
    else
        memset( sequence->intra_quantiser_matrix, 0, 64 );
    sequence->load_non_intra_quantiser_matrix       = !!(*p & 0x01);
    if( sequence->load_non_intra_quantiser_matrix )
        for( int i = 0; i < 64; ++i, ++p )
            sequence->non_intra_quantiser_matrix[i] =    *p;
    else
        memset( sequence->non_intra_quantiser_matrix, 0, 64 );
}

static void read_gop_header( uint8_t *data, mpeg_video_gop_header_t *gop )
{
    gop->time_code   = (data[0] << 17) | (data[1] << 9) | (data[2] << 1) | ((data[3] & 0x80) >> 7);
    gop->closed_gop  = !!(data[3] & 0x40);
    gop->broken_link = !!(data[3] & 0x20);
}

static void read_picture_header( uint8_t *data, mpeg_video_picture_header_t *picture )
{
    picture->temporal_reference           = (data[0] << 2) | ((data[1] & 0xC0) >> 6);
    picture->picture_coding_type          = (data[1] & 0x38) >> 3;
    picture->vbv_delay                    = (data[1] & 0x07) << 13 | data[2] << 5 | (data[3] & 0xF8) >> 3;
    uint8_t *p = &(data[3]);
    if( picture->picture_coding_type == MPEG_VIDEO_P_FRAME || picture->picture_coding_type == MPEG_VIDEO_B_FRAME )
    {
        picture->full_pel_forward_vector  = !!(*p & 0x04);
        picture->forward_f_code           =   (*p & 0x03) << 2 | (*(p+1) & 0x80) >> 7;
        ++p;
    }
    else
    {
        picture->full_pel_forward_vector  = 0;
        picture->forward_f_code           = 0;
    }
    if( picture->picture_coding_type == MPEG_VIDEO_B_FRAME )
    {
        picture->full_pel_backword_vector = !!(*p & 0x40);
        picture->backward_f_code          =   (*p & 0x38) >> 3;;
    }
    else
    {
        picture->full_pel_backword_vector = 0;
        picture->backward_f_code          = 0;
    }
#if 0
#define get_nextbits( p, mask, flag )       \
{                                           \
    flag = !!(*p & mask);                   \
    if( mask == 0x01 )                      \
        mask = 0x80;                        \
    else                                    \
        mask >>= 1;                         \
}
    uint8_t mask = 0x04;
    int extra_bit_picutre;
    get_nextbits( p, mask, extra_bit_picutre );
    while( extra_bit_picutre )
    {
        //uint8_t extra_information_picutre;
        ++p;            /* 8its skip */
        get_nextbits( p, mask, extra_bit_picutre );
    }
#undef get_nextbits
#endif
}

#if 0
static void read_slice_header( uint8_t *data, mpeg_video_slice_header_t *slice )
{
    slice->slice_vertical_position_extension = 0;
    slice->priority_break_point              = 0;
    slice->quantize_scale_code               = 0;
    slice->intra_slice                       = 0;
}
#endif

static void read_sequence_extension( uint8_t *data, mpeg_video_sequence_extension_t *sequence_ext )
{
    sequence_ext->profile_and_level_indication =  ((data[0] & 0x08) << 4) | ((data[1] & 0x80) >> 4);
    sequence_ext->progressive_sequence         = !!(data[1] & 0x08);
    sequence_ext->chroma_format                =   (data[1] & 0x06) >> 1;
    sequence_ext->horizontal_size_extension    =  ((data[1] & 0x01) << 1) | ((data[2] & 0x80) >> 7);
    sequence_ext->vertical_size_extension      =   (data[2] & 0x60) >> 5;
    sequence_ext->bit_rate_extension           =  ((data[2] & 0x1F) << 7) | ((data[3] & 0xFE) >> 1);
    /* marker_bit '1'                          = !!(data[3] & 0x01); */
    sequence_ext->vbv_buffer_size_extension    =    data[4];
    sequence_ext->low_delay                    = !!(data[5] & 0x80);
    sequence_ext->frame_rate_extension_n       =   (data[5] & 0x60) >> 5;
    sequence_ext->frame_rate_extension_d       =   (data[5] & 0x1F);
}

static void read_sequence_display_extension( uint8_t *data, mpeg_video_sequence_display_extension_t *sequence_display_ext )
{
    sequence_display_ext->video_format                 =   (data[0] & 0x0E) >> 1;
    sequence_display_ext->colour_description           = !!(data[0] & 0x01);
    uint8_t *p = &(data[1]);
    if( sequence_display_ext->colour_description )
    {
        sequence_display_ext->colour_primaries         =   *p++;
        sequence_display_ext->transfer_characteristics =   *p++;
        sequence_display_ext->matrix_coefficients      =   *p++;
    }
    else
    {
        sequence_display_ext->colour_primaries         = 0;
        sequence_display_ext->transfer_characteristics = 0;
        sequence_display_ext->matrix_coefficients      = 0;
    }
    sequence_display_ext->display_horizontal_size      =   (p[0] << 6) | (p[1] >> 2);
    /* marker_bit '1'                                  = !!(p[1] & 0x02); */
    sequence_display_ext->display_vertical_size        =  ((p[1] & 0x01) << 13) | (p[2] << 5) | (p[3] >> 3);
}

typedef enum {
    data_partitioning    = 0x00,
    spatial_scalability  = 0x01,
    SNR_scalability      = 0x02,
    temporal_scalability = 0x03
} scalable_mode;

static void read_sequence_scalable_extension( uint8_t *data, mpeg_video_sequence_scalable_extension_t *sequence_scalable_ext )
{
    sequence_scalable_ext->scalable_mode                              =    data[0] & 0x0C >> 2;
    sequence_scalable_ext->layer_id                                   =   (data[0] & 0x03) << 2 | ((data[1] & 0xC0) >> 6);
    if( sequence_scalable_ext->scalable_mode == spatial_scalability )
    {
        sequence_scalable_ext->lower_layer_prediction_horizontal_size =  ((data[1] & 0x3F) << 8) | data[2];
        /* marker_bit '1'                                             = !!(data[3] & 0x80); */
        sequence_scalable_ext->lower_layer_prediction_vertical_size   =  ((data[3] & 0x7F) << 7) | (data[4] >> 1);
        sequence_scalable_ext->horizontal_subsampling_factor_m        =  ((data[4] & 0x01) << 4) | (data[5] >> 4);
        sequence_scalable_ext->horizontal_subsampling_factor_n        =  ((data[4] & 0x0F) << 1) | !!(data[5] & 0x80);
        sequence_scalable_ext->vertical_subsampling_factor_m          =   (data[5] & 0x7C) >> 2;
        sequence_scalable_ext->vertical_subsampling_factor_n          =  ((data[5] & 0x03) << 3) | (data[6] >> 5);
    }
    else if( sequence_scalable_ext->scalable_mode == temporal_scalability )
    {
        sequence_scalable_ext->picture_mux_enable                     = !!(data[1] & 0x20);
        if( sequence_scalable_ext->picture_mux_enable )
        {
            sequence_scalable_ext->mux_to_progressive_sequence        = !!(data[1] & 0x10);
            sequence_scalable_ext->picture_mux_order                  =   (data[1] & 0x0E) >> 1;
            sequence_scalable_ext->picture_mux_factor                 =  ((data[1] & 0x01) << 2) | ((data[2] & 0xC0) >> 6);
        }
        else
        {
            sequence_scalable_ext->mux_to_progressive_sequence        = 0;
            sequence_scalable_ext->picture_mux_order                  =   (data[1] & 0x1C) >> 2;
            sequence_scalable_ext->picture_mux_factor                 =  ((data[1] & 0x03) << 1) | !!(data[2] & 0x80);
        }
    }
}

static void read_picture_coding_extension( uint8_t *data, mpeg_video_picture_coding_extension_t *picture_coding_ext )
{
    picture_coding_ext->f_code[0].horizontal       =    data[0] & 0x0F;
    picture_coding_ext->f_code[0].vertical         =   (data[1] & 0xF0) >> 4;
    picture_coding_ext->f_code[1].horizontal       =    data[1] & 0x0F;
    picture_coding_ext->f_code[1].vertical         =   (data[2] & 0xF0) >> 4;
    picture_coding_ext->intra_dc_precision         =   (data[2] & 0x0C) >> 2;
    picture_coding_ext->picture_structure          =   (data[2] & 0x03);
    picture_coding_ext->top_field_first            = !!(data[3] & 0x80);
    picture_coding_ext->frame_predictive_frame_dct = !!(data[3] & 0x40);
    picture_coding_ext->concealment_motion_vectors = !!(data[3] & 0x20);
    picture_coding_ext->q_scale_type               = !!(data[3] & 0x10);
    picture_coding_ext->intra_vlc_format           = !!(data[3] & 0x08);
    picture_coding_ext->alternate_scan             = !!(data[3] & 0x04);
    picture_coding_ext->repeat_first_field         = !!(data[3] & 0x02);
    picture_coding_ext->chroma_420_type            = !!(data[3] & 0x01);
    picture_coding_ext->progressive_frame          = !!(data[4] & 0x80);
    picture_coding_ext->composite_display_flag     = !!(data[4] & 0x40);
    if( !picture_coding_ext->composite_display_flag )
    {
        picture_coding_ext->v_axis                 = !!(data[4] & 0x20);
        picture_coding_ext->field_sequence         =   (data[4] & 0x1C) >> 2;
        picture_coding_ext->sub_carrier            = !!(data[4] & 0x02);
        picture_coding_ext->burst_amplitude        =  ((data[4] & 0x01) << 6) | (data[5] >> 2);
        picture_coding_ext->sub_carrier_phase      =  ((data[5] & 0x03) << 6) | (data[6] >> 2);
    }
}

static void read_quant_matrix_extension( uint8_t *data, mpeg_video_quant_matrix_extension_t *quant_matrix_ext )
{
    uint8_t *p = &(data[0]);
    quant_matrix_ext->load_intra_quantiser_matrix                  = !!(*p & 0x08);
    if( quant_matrix_ext->load_intra_quantiser_matrix )
        for( int i = 0; i < 64; ++i, ++p )
            quant_matrix_ext->intra_quantiser_matrix[i]            =  ((*p & 0x07) << 5) | (*(p+1) >> 3);
    else
        memset( quant_matrix_ext->intra_quantiser_matrix, 0, 64 );
    quant_matrix_ext->load_non_intra_quantiser_matrix              = !!(*p & 0x04);
    if( quant_matrix_ext->load_non_intra_quantiser_matrix )
        for( int i = 0; i < 64; ++i, ++p )
            quant_matrix_ext->non_intra_quantiser_matrix[i]        =  ((*p & 0x03) << 6) | (*(p+1) >> 2);
    else
        memset( quant_matrix_ext->non_intra_quantiser_matrix, 0, 64 );
    quant_matrix_ext->load_chroma_intra_quantiser_matrix           = !!(*p & 0x02);
    if( quant_matrix_ext->load_chroma_intra_quantiser_matrix )
        for( int i = 0; i < 64; ++i, ++p )
            quant_matrix_ext->chroma_intra_quantiser_matrix[i]     =  ((*p & 0x01) << 7) | (*(p+1) >> 1);
    else
        memset( quant_matrix_ext->chroma_intra_quantiser_matrix, 0, 64 );
    quant_matrix_ext->load_chroma_non_intra_quantiser_matrix       = !!(*p & 0x01);
    if( quant_matrix_ext->load_chroma_non_intra_quantiser_matrix )
        for( int i = 0; i < 64; ++i, ++p )
            quant_matrix_ext->chroma_non_intra_quantiser_matrix[i] =   *p;
    else
        memset( quant_matrix_ext->chroma_non_intra_quantiser_matrix, 0, 64 );
}

#define bit_shift( p, n, m )        \
{                                   \
    if( m == 0x01 )                 \
    {                               \
        m = 0x80;                   \
        ++p;                        \
    }                               \
    else                            \
        m >>= 1;                    \
}
#define getbits( p, n, m, v )       \
{                                   \
    int getbits_count = n;          \
    while( getbits_count )          \
    {                               \
        v = (v << 1) | !!(*p & m);  \
        bit_shift( p, n, m )        \
        --getbits_count;            \
    }                               \
}
static void read_picture_display_extension( uint8_t *data, mpeg_video_picture_display_extension_t *picture_display_ext )
{
    uint8_t number_of_frame_centre_offsets = picture_display_ext->number_of_frame_centre_offsets;
    /* initialize. */
    memset( picture_display_ext, 0, sizeof(mpeg_video_picture_display_extension_t) );
    if( !number_of_frame_centre_offsets )
        return;
    picture_display_ext->number_of_frame_centre_offsets = number_of_frame_centre_offsets;
    /* read. */
    uint8_t *p = &(data[0]);
    uint8_t bit_start = 0x08;
    for( int i = 0; i < number_of_frame_centre_offsets; ++i )
    {
        getbits( p, 14, bit_start, picture_display_ext->frame_centre_offsets[i].horizontal_offset )
        bit_shift( p, 1, bit_start )        /* marker_bit '1' */
        getbits( p, 14, bit_start, picture_display_ext->frame_centre_offsets[i].vertical_offset );
        bit_shift( p, 1, bit_start )        /* marker_bit '1' */
    }
}
#undef bit_shift
#undef getbits

static void read_picture_temporal_scalable_extension( uint8_t *data, mpeg_video_picture_temporal_scalable_extension_t *picture_temporal_scalable_ext )
{
    picture_temporal_scalable_ext->reference_select_code       =   (data[0] & 0x0C) >> 2;
    picture_temporal_scalable_ext->forward_temporal_reference  =  ((data[0] & 0x03) << 8) | data[1];
    /* marker_bit '1'                                          = !!(data[2] & 0x80); */
    picture_temporal_scalable_ext->backward_temporal_reference =   (data[2] & 0x7F) << 3 | (data[3] >> 5);
}

static void read_picture_spatial_scalable_extension( uint8_t *data, mpeg_video_picture_spatial_scalable_extension_t *picture_spatial_scalable_ext )
{
    picture_spatial_scalable_ext->lower_layer_temporal_reference           =  ((data[0] & 0x08) << 6) | (data[1] >> 2);
    /* marker_bit '1'                                                      = !!(data[1] & 0x02); */
    picture_spatial_scalable_ext->lower_layer_horizontal_offset            =  ((data[1] & 0x01) << 14) | (data[2] << 6) | (data[3] >> 2);
    /* marker_bit '1'                                                      = !!(data[3] & 0x02); */
    picture_spatial_scalable_ext->lower_layer_vertical_offset              =  ((data[3] & 0x01) << 14) | (data[4] << 6) | (data[5] >> 2);
    picture_spatial_scalable_ext->spatial_temporal_weight_code_table_index =    data[5] & 0x02;
    picture_spatial_scalable_ext->lower_layer_progressive_frame            = !!(data[6] & 0x80);
    picture_spatial_scalable_ext->lower_layer_deinterlaced_field_select    = !!(data[6] & 0x40);
}

static void read_copyright_extension( uint8_t *data, mpeg_video_copyright_extension_t *copyright_ext )
{
    copyright_ext->copyright_flag       = !!(data[0] & 0x08);
    copyright_ext->copyright_identifier =  ((data[0] & 0x07) << 5) | (data[1] >> 3);
    copyright_ext->original_or_copy     = !!(data[1] & 0x04);
    /* reserved     7bit                =  ((data[1] & 0x03) << 5) | (data[2] >> 3); */
    /* marker_bit '1'                   = !!(data[2] & 0x04); */
    copyright_ext->copyright_number_1   =  ((data[2] & 0x03) << 20) | (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
    /* marker_bit '1'                   = !!(data[5] & 0x08); */
    copyright_ext->copyright_number_2   =  ((data[5] & 0x07) << 19) | (data[6] << 11) | (data[7] << 3) | (data[7] >> 5);
    /* marker_bit '1'                   = !!(data[7] & 0x40); */
    copyright_ext->copyright_number_3   =  ((data[8] & 0x3F) << 16) | (data[9] << 8) | data[10];
}

static uint8_t get_number_of_frame_centre_offsets( mpeg_video_info_t *video_info )
{
    uint8_t number_of_frame_centre_offsets = 0;
    uint8_t progressive_sequence = video_info->sequence_ext.progressive_sequence;
    uint8_t repeat_first_field   = video_info->picture_coding_ext.repeat_first_field;
    uint8_t top_field_first      = video_info->picture_coding_ext.top_field_first;
    uint8_t picture_structure    = video_info->picture_coding_ext.picture_structure;
    typedef enum {
        Top_Field    = 0x01,
        Bottom_Field = 0x02,
        Frame        = 0x03
    } picture_structure_type;
    if( progressive_sequence )
    {
        if( repeat_first_field )
        {
            if( top_field_first )
                number_of_frame_centre_offsets = 3;
            else
                number_of_frame_centre_offsets = 2;
        }
        else
            number_of_frame_centre_offsets = 1;
    }
    else
    {
        if( picture_structure != Frame )
            number_of_frame_centre_offsets = 1;
        else
        {
            if( repeat_first_field )
                number_of_frame_centre_offsets = 3;
            else
                number_of_frame_centre_offsets = 2;
        }
    }
    return number_of_frame_centre_offsets;
}

typedef enum {
    /* 0000 reserved                        = 0x00      */
    Sequence_Extension_ID                   = 0x01,
    Sequence_Display_Extension_ID           = 0x02,
    Quant_Matrix_Extension_ID               = 0x03,
    Copyright_Extension_ID                  = 0x04,
    Sequence_Scalable_Extension_ID          = 0x05,
    /* 0110 reserved                        = 0x06      */
    Picture_Display_Extension_ID            = 0x07,
    Picture_Coding_Extension_ID             = 0x08,
    Picture_Spatial_Scalable_Extension_ID   = 0x09,
    Picture_Temporal_Scalable_Extension_ID  = 0x0A
    /* 1011 reserved                        = 0x0B      */
    /* 1100 reserved                        = 0x0C      */
    /*  ...                                             */
    /* 1111 reserved                        = 0x0F      */
} extension_start_code_identifier_code;

static void mpeg_video_get_extension_info( uint8_t *buf, mpeg_video_info_t *video_info )
{
    extension_start_code_identifier_code extension_start_code_identifier = buf[0] >> 4;
    switch( extension_start_code_identifier )
    {
        /* Sequence Header */
        case Sequence_Extension_ID :
            read_sequence_extension( buf, &(video_info->sequence_ext) );
            break;
        case Sequence_Display_Extension_ID :
            read_sequence_display_extension( buf, &(video_info->sequence_display_ext) );
            break;
        case Sequence_Scalable_Extension_ID :
            read_sequence_scalable_extension( buf, &(video_info->sequence_scalable_ext) );
            break;
        /* Picture Header */
        case Picture_Coding_Extension_ID :
            read_picture_coding_extension( buf, &(video_info->picture_coding_ext) );
            break;
        case Quant_Matrix_Extension_ID :
            read_quant_matrix_extension( buf, &(video_info->quant_matrix_ext) );
            break;
        case Picture_Display_Extension_ID :
            video_info->picture_display_ext.number_of_frame_centre_offsets = get_number_of_frame_centre_offsets( video_info );
            read_picture_display_extension( buf, &(video_info->picture_display_ext) );
            break;
        case Picture_Temporal_Scalable_Extension_ID :
            read_picture_temporal_scalable_extension( buf, &(video_info->picture_temporal_scalable_ext) );
            break;
        case Picture_Spatial_Scalable_Extension_ID :
            read_picture_spatial_scalable_extension( buf, &(video_info->picture_spatial_scalable_ext) );
            break;
        case Copyright_Extension_ID :
            read_copyright_extension( buf, &(video_info->copyright_ext) );
            break;
        default:
            break;
    }
}

extern mpeg_video_extension_type mpeg_video_check_extension_start_code_identifier( uint8_t identifier_buf )
{
    mpeg_video_extension_type extension_type = NON_EXTENSTION;
    extension_start_code_identifier_code extension_start_code_identifier = identifier_buf >> 4;
    switch( extension_start_code_identifier )
    {
        /* Sequence Header */
        case Sequence_Extension_ID :
            extension_type = SEQUENCE_EXT;
            break;
        case Sequence_Display_Extension_ID :
            extension_type = SEQUENCE_DISPLAY_EXT;
            break;
        case Sequence_Scalable_Extension_ID :
            extension_type = SEQUENCE_SCALABLE_EXT;
            break;
        /* Picture Header */
        case Picture_Coding_Extension_ID :
            extension_type = PICTURE_CODING_EXT;
            break;
        case Quant_Matrix_Extension_ID :
            extension_type = QUANT_MATRIX_EXT;
            break;
        case Picture_Display_Extension_ID :
            extension_type = PICTURE_DISPLAY_EXT;
            break;
        case Picture_Temporal_Scalable_Extension_ID :
            extension_type = PICTURE_TEMPORAL_SCALABLE_EXT;
            break;
        case Picture_Spatial_Scalable_Extension_ID :
            extension_type = PICTURE_SPATIAL_SCALABLE_EXT;
            break;
        case Copyright_Extension_ID :
            extension_type = COPYRIGHT_EXT;
            break;
        default:
            break;
    }
    return extension_type;
}

extern void mpeg_video_get_header_info( uint8_t *buf, mpeg_video_star_code_type start_code, mpeg_video_info_t *video_info )
{
    switch( start_code )
    {
        case MPEG_VIDEO_START_CODE_SHC :
            read_sequence_header( buf, &(video_info->sequence) );
            break;
        case MPEG_VIDEO_START_CODE_ESC :
            mpeg_video_get_extension_info( buf, video_info );
            break;
        case MPEG_VIDEO_START_CODE_UDSC :
            break;
        case MPEG_VIDEO_START_CODE_SEC :
            break;
        case MPEG_VIDEO_START_CODE_GSC :
            read_gop_header( buf, &(video_info->gop) );
            break;
        case MPEG_VIDEO_START_CODE_PSC :
            read_picture_header( buf, &(video_info->picture) );
            break;
        case MPEG_VIDEO_START_CODE_SSC :
#if 0
            read_slice_header( buf, &(video_info->slice) );
#endif
            break;
        default :
            break;
    }
}
