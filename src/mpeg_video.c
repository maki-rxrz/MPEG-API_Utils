/*****************************************************************************
 * mpeg_video.c
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

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "mpeg_common.h"
#include "mpeg_video.h"

static const uint8_t video_start_code_common_head[MPEG_VIDEO_START_CODE_SIZE - 1] = { 0x00, 0x00, 0x01 };

extern int mpeg_video_check_start_code_common_head( uint8_t *start_code )
{
    if( memcmp( start_code, video_start_code_common_head, MPEG_VIDEO_START_CODE_SIZE - 1 ) )
        return -1;
    return 0;
}

extern int mpeg_video_check_start_code( uint8_t *start_code, mpeg_video_start_code_type start_code_type )
{
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
    if( memcmp( start_code, video_start_code_common_head, MPEG_VIDEO_START_CODE_SIZE - 1 ) )
        return -1;
    if( start_code_type == MPEG_VIDEO_START_CODE_SSC )
    {
        if( start_code[MPEG_VIDEO_START_CODE_SIZE - 1] < mpeg_video_start_code[start_code_type + 0]
         || start_code[MPEG_VIDEO_START_CODE_SIZE - 1] > mpeg_video_start_code[start_code_type + 1] )
            return -1;
    }
    else if( start_code[MPEG_VIDEO_START_CODE_SIZE - 1] != mpeg_video_start_code[start_code_type] )
        return -1;
    return 0;
}

static int32_t read_sequence_header( uint8_t *data, mpeg_video_sequence_header_t *sequence )
{
    int32_t check_size = MPEG_VIDEO_SEQUENCE_SECTION_HEADER_SIZE;
    sequence->horizontal_size             =   (data[0] << 4) | ((data[1] & 0xF0) >> 4);
    sequence->vertical_size               =  ((data[1] & 0x0F) << 8) | (data[2]);
    sequence->aspect_ratio_information    =   (data[3] & 0xF0) >> 4;
    sequence->frame_rate_code             =    data[3] & 0x0F;
    sequence->bit_rate                    =   (data[4] << 10) | (data[5] << 2) | (data[6] >> 6);
    /* marker_bit '1'                     = !!(data[6] & 0x20); */
    sequence->vbv_buffer_size             =  ((data[6] & 0x1F) << 5) | (data[7] >> 3);
    sequence->constrained_parameters_flag = !!(data[7] & 0x04);
    sequence->load_intra_quantiser_matrix = !!(data[7] & 0x02);
    uint8_t *p = &(data[7]);
    if( sequence->load_intra_quantiser_matrix )
        for( int i = 0; i < 64; ++i, ++p )
            sequence->intra_quantiser_matrix[i]     =   (*p & 0x01) << 7 | *(p+1) >> 1;
    else
    {
        memset( sequence->intra_quantiser_matrix, 0, 64 );
        check_size -= 64;
    }
    sequence->load_non_intra_quantiser_matrix       = !!(*p & 0x01);
    ++p;
    if( sequence->load_non_intra_quantiser_matrix )
        for( int i = 0; i < 64; ++i, ++p )
            sequence->non_intra_quantiser_matrix[i] =    *p;
    else
    {
        memset( sequence->non_intra_quantiser_matrix, 0, 64 );
        check_size -= 64;
    }
    return check_size;
}

static int32_t read_gop_header( uint8_t *data, mpeg_video_gop_header_t *gop )
{
    gop->time_code   = (data[0] << 17) | (data[1] << 9) | (data[2] << 1) | (data[3] >> 7);
    gop->closed_gop  = !!(data[3] & 0x40);
    gop->broken_link = !!(data[3] & 0x20);
    return MPEG_VIDEO_GOP_SECTION_HEADER_SIZE;
}

static int32_t read_picture_header( uint8_t *data, mpeg_video_picture_header_t *picture )
{
    int32_t check_size = MPEG_VIDEO_PICTURE_SECTION_HEADER_SIZE;
    picture->temporal_reference           =  (data[0] << 2) | (data[1] >> 6);
    picture->picture_coding_type          =  (data[1] & 0x38) >> 3;
    picture->vbv_delay                    = ((data[1] & 0x07) << 13) | (data[2] << 5) | (data[3] >> 3);
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
        --check_size;
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
    int count = 0;
    while( extra_bit_picutre )
    {
        if( mask = 0x80 )
            ++count;
        ++count;
        //uint8_t extra_information_picutre;
        ++p;            /* 8its skip */
        get_nextbits( p, mask, extra_bit_picutre );
    }
    check_size += count;
#undef get_nextbits
#endif
    return check_size;
}

static int64_t read_slice_header( /* uint8_t *data, */ mpeg_video_slice_header_t *slice )
{
    /* No reading header data. */           // FIXME
    slice->slice_vertical_position_extension = 0;
    slice->priority_breakpoint               = 0;
    slice->quantiser_scale_code              = 0;
    slice->intra_slice                       = 0;
    return MPEG_VIDEO_SLICE_SECTION_HEADER_SIZE;
}

static int32_t read_sequence_extension( uint8_t *data, mpeg_video_sequence_extension_t *sequence_ext )
{
    sequence_ext->profile_and_level_indication =  ((data[0] & 0x0F) << 4) | (data[1] >> 4);
    sequence_ext->progressive_sequence         = !!(data[1] & 0x08);
    sequence_ext->chroma_format                =   (data[1] & 0x06) >> 1;
    sequence_ext->horizontal_size_extension    =  ((data[1] & 0x01) << 1) | !!(data[2] & 0x80);
    sequence_ext->vertical_size_extension      =   (data[2] & 0x60) >> 5;
    sequence_ext->bit_rate_extension           =  ((data[2] & 0x1F) << 7) | ((data[3] & 0xFE) >> 1);
    /* marker_bit '1'                          = !!(data[3] & 0x01); */
    sequence_ext->vbv_buffer_size_extension    =    data[4];
    sequence_ext->low_delay                    = !!(data[5] & 0x80);
    sequence_ext->frame_rate_extension_n       =   (data[5] & 0x60) >> 5;
    sequence_ext->frame_rate_extension_d       =   (data[5] & 0x1F);
    return MPEG_VIDEO_SEQUENCE_EXTENSION_SIZE;
}

static int32_t read_sequence_display_extension
(
    uint8_t                                    *data,
    mpeg_video_sequence_display_extension_t    *sequence_display_ext
)
{
    int32_t check_size = MPEG_VIDEO_SEQUENCE_DISPLAY_EXTENSION_SIZE;
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
        check_size -= 3;
    }
    sequence_display_ext->display_horizontal_size      =   (p[0] << 6) | (p[1] >> 2);
    /* marker_bit '1'                                  = !!(p[1] & 0x02); */
    sequence_display_ext->display_vertical_size        =  ((p[1] & 0x01) << 13) | (p[2] << 5) | (p[3] >> 3);
    return check_size;
}

typedef enum {
    data_partitioning    = 0x00,
    spatial_scalability  = 0x01,
    SNR_scalability      = 0x02,
    temporal_scalability = 0x03
} scalable_mode;

static int32_t read_sequence_scalable_extension
(
    uint8_t                                    *data,
    mpeg_video_sequence_scalable_extension_t   *sequence_scalable_ext
)
{
    int32_t check_size = MPEG_VIDEO_SEQUENCE_SCALABLE_EXTENSION_SIZE;
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
        check_size = 3;
    }
    return check_size;
}

static int32_t read_picture_coding_extension
(
    uint8_t                                *data,
    mpeg_video_picture_coding_extension_t  *picture_coding_ext
)
{
    int32_t check_size = MPEG_VIDEO_PICTURE_CODING_EXTENSION_SIZE;
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
    if( picture_coding_ext->composite_display_flag )
    {
        picture_coding_ext->v_axis                 = !!(data[4] & 0x20);
        picture_coding_ext->field_sequence         =   (data[4] & 0x1C) >> 2;
        picture_coding_ext->sub_carrier            = !!(data[4] & 0x02);
        picture_coding_ext->burst_amplitude        =  ((data[4] & 0x01) << 6) | (data[5] >> 2);
        picture_coding_ext->sub_carrier_phase      =  ((data[5] & 0x03) << 6) | (data[6] >> 2);
    }
    else
    {
        picture_coding_ext->v_axis                 = 0;
        picture_coding_ext->field_sequence         = 0;
        picture_coding_ext->sub_carrier            = 0;
        picture_coding_ext->burst_amplitude        = 0;
        picture_coding_ext->sub_carrier_phase      = 0;
        check_size -= 2;
    }
    return check_size;
}

static int32_t read_quant_matrix_extension
(
    uint8_t                                *data,
    mpeg_video_quant_matrix_extension_t    *quant_matrix_ext
)
{
    int32_t check_size = MPEG_VIDEO_QUANT_MATRIX_EXTENSION_SIZE;
    uint8_t *p = &(data[0]);
    quant_matrix_ext->load_intra_quantiser_matrix                  = !!(*p & 0x08);
    if( quant_matrix_ext->load_intra_quantiser_matrix )
        for( int i = 0; i < 64; ++i, ++p )
            quant_matrix_ext->intra_quantiser_matrix[i]            =  ((*p & 0x07) << 5) | (*(p+1) >> 3);
    else
    {
        memset( quant_matrix_ext->intra_quantiser_matrix, 0, 64 );
        check_size -= 64;
    }
    quant_matrix_ext->load_non_intra_quantiser_matrix              = !!(*p & 0x04);
    if( quant_matrix_ext->load_non_intra_quantiser_matrix )
        for( int i = 0; i < 64; ++i, ++p )
            quant_matrix_ext->non_intra_quantiser_matrix[i]        =  ((*p & 0x03) << 6) | (*(p+1) >> 2);
    else
    {
        memset( quant_matrix_ext->non_intra_quantiser_matrix, 0, 64 );
        check_size -= 64;
    }
    quant_matrix_ext->load_chroma_intra_quantiser_matrix           = !!(*p & 0x02);
    if( quant_matrix_ext->load_chroma_intra_quantiser_matrix )
        for( int i = 0; i < 64; ++i, ++p )
            quant_matrix_ext->chroma_intra_quantiser_matrix[i]     =  ((*p & 0x01) << 7) | (*(p+1) >> 1);
    else
    {
        memset( quant_matrix_ext->chroma_intra_quantiser_matrix, 0, 64 );
        check_size -= 64;
    }
    quant_matrix_ext->load_chroma_non_intra_quantiser_matrix       = !!(*p & 0x01);
    ++p;
    if( quant_matrix_ext->load_chroma_non_intra_quantiser_matrix )
        for( int i = 0; i < 64; ++i, ++p )
            quant_matrix_ext->chroma_non_intra_quantiser_matrix[i] =   *p;
    else
    {
        memset( quant_matrix_ext->chroma_non_intra_quantiser_matrix, 0, 64 );
        check_size -= 64;
    }
    return check_size;
}

#define bit_shift( p, n, m )        \
do {                                \
    if( m == 0x01 )                 \
    {                               \
        m = 0x80;                   \
        ++p;                        \
    }                               \
    else                            \
        m >>= 1;                    \
} while( 0 )
#define getbits( p, n, m, v )       \
do {                                \
    int getbits_count = n;          \
    while( getbits_count )          \
    {                               \
        v = (v << 1) | !!(*p & m);  \
        bit_shift( p, n, m );       \
        --getbits_count;            \
    }                               \
} while( 0 )
static int32_t read_picture_display_extension
(
    uint8_t                                    *data,
    mpeg_video_picture_display_extension_t     *picture_display_ext
)
{
    uint8_t number_of_frame_centre_offsets = picture_display_ext->number_of_frame_centre_offsets;
    /* initialize. */
    memset( picture_display_ext, 0, sizeof(mpeg_video_picture_display_extension_t) );
    if( !number_of_frame_centre_offsets )
        return 0;
    picture_display_ext->number_of_frame_centre_offsets = number_of_frame_centre_offsets;
    /* read. */
    uint8_t *p = &(data[0]);
    uint8_t bit_start = 0x08;
    for( int i = 0; i < number_of_frame_centre_offsets; ++i )
    {
        getbits( p, 14, bit_start, picture_display_ext->frame_centre_offsets[i].horizontal_offset );
        bit_shift( p, 1, bit_start );       /* marker_bit '1' */
        getbits( p, 14, bit_start, picture_display_ext->frame_centre_offsets[i].vertical_offset );
        bit_shift( p, 1, bit_start );       /* marker_bit '1' */
    }
    return (number_of_frame_centre_offsets == 3) ? 14 :   /* 4bits + 34bits * 3 */
           (number_of_frame_centre_offsets == 2) ?  9 :   /* 4bits + 34bits * 2 */
           (number_of_frame_centre_offsets == 1) ?  5 :   /* 4bits + 34bits * 1 */
                                                    1;    /* 4bits              */
}
#undef bit_shift
#undef getbits

static int32_t read_picture_temporal_scalable_extension
(
    uint8_t                                            *data,
    mpeg_video_picture_temporal_scalable_extension_t   *picture_temporal_scalable_ext
)
{
    picture_temporal_scalable_ext->reference_select_code       =   (data[0] & 0x0C) >> 2;
    picture_temporal_scalable_ext->forward_temporal_reference  =  ((data[0] & 0x03) << 8) | data[1];
    /* marker_bit '1'                                          = !!(data[2] & 0x80); */
    picture_temporal_scalable_ext->backward_temporal_reference =   (data[2] & 0x7F) << 3 | (data[3] >> 5);
    return MPEG_VIDEO_PICTURE_TEMPORAL_SCALABLE_EXTENSION_SIZE;
}

static int32_t read_picture_spatial_scalable_extension
(
    uint8_t                                            *data,
    mpeg_video_picture_spatial_scalable_extension_t    *picture_spatial_scalable_ext
)
{
    picture_spatial_scalable_ext->lower_layer_temporal_reference           =  ((data[0] & 0x08) << 6)
                                                                           |   (data[1]         >> 2);
    /* marker_bit '1'                                                      = !!(data[1] & 0x02); */
    picture_spatial_scalable_ext->lower_layer_horizontal_offset            =  ((data[1] & 0x01) << 14)
                                                                           |   (data[2]         <<  6)
                                                                           |   (data[3]         >>  2);
    /* marker_bit '1'                                                      = !!(data[3] & 0x02); */
    picture_spatial_scalable_ext->lower_layer_vertical_offset              =  ((data[3] & 0x01) << 14)
                                                                           |   (data[4]         <<  6)
                                                                           |   (data[5]         >>  2);
    picture_spatial_scalable_ext->spatial_temporal_weight_code_table_index =    data[5] & 0x02;
    picture_spatial_scalable_ext->lower_layer_progressive_frame            = !!(data[6] & 0x80);
    picture_spatial_scalable_ext->lower_layer_deinterlaced_field_select    = !!(data[6] & 0x40);
    return MPEG_VIDEO_PICTURE_SPATIAL_SCALABLE_EXTENSION_SIZE;
}

static int32_t read_copyright_extension( uint8_t *data, mpeg_video_copyright_extension_t *copyright_ext )
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
    return MPEG_VIDEO_COPYRIGHT_EXTENSION_SIZE;
}

static uint8_t get_number_of_frame_centre_offsets( mpeg_video_info_t *video_info )
{
    uint8_t number_of_frame_centre_offsets = 0;
    uint8_t progressive_sequence = video_info->sequence_ext.progressive_sequence;
    uint8_t repeat_first_field   = video_info->picture_coding_ext.repeat_first_field;
    uint8_t top_field_first      = video_info->picture_coding_ext.top_field_first;
    uint8_t picture_structure    = video_info->picture_coding_ext.picture_structure;
    enum {
        Top_Field    = 0x01,
        Bottom_Field = 0x02,
        Frame        = 0x03
    };
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

static int32_t mpeg_video_get_extension_info( uint8_t *buf, mpeg_video_info_t *video_info )
{
    int32_t check_size = 0;
    extension_start_code_identifier_code extension_start_code_identifier = buf[0] >> 4;
    switch( extension_start_code_identifier )
    {
        /* Sequence Header */
        case Sequence_Extension_ID :
            check_size = read_sequence_extension( buf, &(video_info->sequence_ext) );
            break;
        case Sequence_Display_Extension_ID :
            check_size = read_sequence_display_extension( buf, &(video_info->sequence_display_ext) );
            break;
        case Sequence_Scalable_Extension_ID :
            check_size = read_sequence_scalable_extension( buf, &(video_info->sequence_scalable_ext) );
            break;
        /* Picture Header */
        case Picture_Coding_Extension_ID :
            check_size = read_picture_coding_extension( buf, &(video_info->picture_coding_ext) );
            break;
        case Quant_Matrix_Extension_ID :
            check_size = read_quant_matrix_extension( buf, &(video_info->quant_matrix_ext) );
            break;
        case Picture_Display_Extension_ID :
            video_info->picture_display_ext.number_of_frame_centre_offsets = get_number_of_frame_centre_offsets( video_info );
            check_size = read_picture_display_extension( buf, &(video_info->picture_display_ext) );
            break;
        case Picture_Temporal_Scalable_Extension_ID :
            check_size = read_picture_temporal_scalable_extension( buf, &(video_info->picture_temporal_scalable_ext) );
            break;
        case Picture_Spatial_Scalable_Extension_ID :
            check_size = read_picture_spatial_scalable_extension( buf, &(video_info->picture_spatial_scalable_ext) );
            break;
        case Copyright_Extension_ID :
            check_size = read_copyright_extension( buf, &(video_info->copyright_ext) );
            break;
        default:
            break;
    }
    return check_size;
}

static mpeg_video_extension_type mpeg_video_check_extension_start_code_identifier( uint8_t identifier )
{
    mpeg_video_extension_type extension_type = NON_EXTENSTION;
    extension_start_code_identifier_code extension_start_code_identifier = identifier >> 4;
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

extern int32_t mpeg_video_get_header_info
(
    uint8_t                        *buf,
    mpeg_video_start_code_type      start_code,
    mpeg_video_info_t              *video_info
)
{
    int32_t check_size = 0;
    switch( start_code )
    {
        case MPEG_VIDEO_START_CODE_SHC :
            check_size = read_sequence_header( buf, &(video_info->sequence) );
            break;
        case MPEG_VIDEO_START_CODE_ESC :
            check_size = mpeg_video_get_extension_info( buf, video_info );
            break;
        case MPEG_VIDEO_START_CODE_UDSC :
            break;
        case MPEG_VIDEO_START_CODE_SEC :
            break;
        case MPEG_VIDEO_START_CODE_GSC :
            check_size = read_gop_header( buf, &(video_info->gop) );
            break;
        case MPEG_VIDEO_START_CODE_PSC :
            check_size = read_picture_header( buf, &(video_info->picture) );
            break;
        case MPEG_VIDEO_START_CODE_SSC :
            check_size = read_slice_header( /* buf, */ &(video_info->slice) );
            break;
        default :
            break;
    }
    return check_size;
}

extern void mpeg_video_debug_header_info
(
    mpeg_video_info_t                          *video_info,
    mpeg_video_start_code_searching_status      searching_status
)
{
    /* debug. */
    switch( searching_status )
    {
        case DETECT_SHC :
            mapi_log( LOG_LV2,
                      "[check] detect Sequence Start Code.\n"
                      "        frame_size:%ux%u\n"
                      "        aspect_ratio_code:%u\n"
                      "        frame_rate_code:%u\n"
                      "        bit_rate:%u\n"
                      "        vbv_buffer_size:%u\n"
                      "        constrained_parameters_flag:%u\n"
                      "        load_intra_quantiser_matrix:%u\n"
                      "        load_non_intra_quantiser_matrix:%u\n"
                      , video_info->sequence.horizontal_size
                      , video_info->sequence.vertical_size
                      , video_info->sequence.aspect_ratio_information
                      , video_info->sequence.frame_rate_code
                      , video_info->sequence.bit_rate
                      , video_info->sequence.vbv_buffer_size
                      , video_info->sequence.constrained_parameters_flag
                      , video_info->sequence.load_intra_quantiser_matrix
                      , video_info->sequence.load_non_intra_quantiser_matrix );
            break;
        case DETECT_SESC :
            mapi_log( LOG_LV2,
                      "        profile_and_level_indication:%u\n"
                      "        progressive_sequence:%u\n"
                      "        chroma_format:%u\n"
                      "        horizontal_size_extension:%u\n"
                      "        vertical_size_extension:%u\n"
                      "        bit_rate_extension:%u\n"
                      "        vbv_buffer_size_extension:%u\n"
                      "        low_delay:%u\n"
                      "        frame_rate_extension_n:%u\n"
                      "        frame_rate_extension_d:%u\n"
                      , video_info->sequence_ext.profile_and_level_indication
                      , video_info->sequence_ext.progressive_sequence
                      , video_info->sequence_ext.chroma_format
                      , video_info->sequence_ext.horizontal_size_extension
                      , video_info->sequence_ext.vertical_size_extension
                      , video_info->sequence_ext.bit_rate_extension
                      , video_info->sequence_ext.vbv_buffer_size_extension
                      , video_info->sequence_ext.low_delay
                      , video_info->sequence_ext.frame_rate_extension_n
                      , video_info->sequence_ext.frame_rate_extension_d );
            break;
        case DETECT_SDE :
            mapi_log( LOG_LV2,
                      "        video_format:%u\n"
                      "        colour_description:%u\n"
                      "        colour_primaries:%u\n"
                      "        transfer_characteristics:%u\n"
                      "        matrix_coefficients:%u\n"
                      "        display_horizontal_size:%u\n"
                      "        display_vertical_size:%u\n"
                      , video_info->sequence_display_ext.video_format
                      , video_info->sequence_display_ext.colour_description
                      , video_info->sequence_display_ext.colour_primaries
                      , video_info->sequence_display_ext.transfer_characteristics
                      , video_info->sequence_display_ext.matrix_coefficients
                      , video_info->sequence_display_ext.display_horizontal_size
                      , video_info->sequence_display_ext.display_vertical_size );
            break;
        case DETECT_SSE :
            mapi_log( LOG_LV2,
                      "        scalable_mode:%u\n"
                      "        layer_id:%u\n"
                      "        lower_layer_prediction_horizontal_size:%u\n"
                      "        lower_layer_prediction_vertical_size:%u\n"
                      "        horizontal_subsampling_factor_m:%u\n"
                      "        horizontal_subsampling_factor_n:%u\n"
                      "        vertical_subsampling_factor_m:%u\n"
                      "        vertical_subsampling_factor_n:%u\n"
                      "        picture_mux_enable:%u\n"
                      "        mux_to_progressive_sequence:%u\n"
                      "        picture_mux_order:%u\n"
                      "        picture_mux_factor:%u\n"
                      , video_info->sequence_scalable_ext.scalable_mode
                      , video_info->sequence_scalable_ext.layer_id
                      , video_info->sequence_scalable_ext.lower_layer_prediction_horizontal_size
                      , video_info->sequence_scalable_ext.lower_layer_prediction_vertical_size
                      , video_info->sequence_scalable_ext.horizontal_subsampling_factor_m
                      , video_info->sequence_scalable_ext.horizontal_subsampling_factor_n
                      , video_info->sequence_scalable_ext.vertical_subsampling_factor_m
                      , video_info->sequence_scalable_ext.vertical_subsampling_factor_n
                      , video_info->sequence_scalable_ext.picture_mux_enable
                      , video_info->sequence_scalable_ext.mux_to_progressive_sequence
                      , video_info->sequence_scalable_ext.picture_mux_order
                      , video_info->sequence_scalable_ext.picture_mux_factor );
            break;
        case DETECT_ESC :
        case DETECT_UDSC :
        case DETECT_SEC :
            break;
        case DETECT_GSC :
            mapi_log( LOG_LV2,
                      "[check] detect GOP Start Code.\n"
                      "        time_code:%u\n"
                      "        closed_gop:%u\n"
                      "        broken_link:%u\n"
                      , video_info->gop.time_code
                      , video_info->gop.closed_gop
                      , video_info->gop.broken_link );
            break;
        case DETECT_PSC :
            mapi_log( LOG_LV2,
                      "[check] detect Picture Start Code.\n"
                      "        temporal_reference:%d\n"
                      "        picture_coding_type:%u\n"
                      "        vbv_delay:%u\n"
                      "        full_pel_forward_vector:%u\n"
                      "        forward_f_code:%u\n"
                      "        full_pel_backword_vector:%u\n"
                      "        backward_f_code:%u\n"
                      , video_info->picture.temporal_reference
                      , video_info->picture.picture_coding_type
                      , video_info->picture.vbv_delay
                      , video_info->picture.full_pel_forward_vector
                      , video_info->picture.forward_f_code
                      , video_info->picture.full_pel_backword_vector
                      , video_info->picture.backward_f_code );
            break;
        case DETECT_PCESC :
            mapi_log( LOG_LV2,
                      "        forward_horizontal:%u\n"
                      "        forward_vertical:%u\n"
                      "        backward_horizontal:%u\n"
                      "        backward_vertical:%u\n"
                      "        intra_dc_precision:%u\n"
                      "        picture_structure:%u\n"
                      "        top_field_first:%u\n"
                      "        frame_predictive_frame_dct:%u\n"
                      "        concealment_motion_vectors:%u\n"
                      "        q_scale_type:%u\n"
                      "        intra_vlc_format:%u\n"
                      "        alternate_scan:%u\n"
                      "        repeat_first_field:%u\n"
                      "        chroma_420_type:%u\n"
                      "        progressive_frame:%u\n"
                      "        composite_display_flag:%u\n"
                      "        v_axis:%u\n"
                      "        field_sequence:%u\n"
                      "        sub_carrier:%u\n"
                      "        burst_amplitude:%u\n"
                      "        sub_carrier_phase:%u\n"
                      , video_info->picture_coding_ext.f_code[0].horizontal
                      , video_info->picture_coding_ext.f_code[0].vertical
                      , video_info->picture_coding_ext.f_code[1].horizontal
                      , video_info->picture_coding_ext.f_code[1].vertical
                      , video_info->picture_coding_ext.intra_dc_precision
                      , video_info->picture_coding_ext.picture_structure
                      , video_info->picture_coding_ext.top_field_first
                      , video_info->picture_coding_ext.frame_predictive_frame_dct
                      , video_info->picture_coding_ext.concealment_motion_vectors
                      , video_info->picture_coding_ext.q_scale_type
                      , video_info->picture_coding_ext.intra_vlc_format
                      , video_info->picture_coding_ext.alternate_scan
                      , video_info->picture_coding_ext.repeat_first_field
                      , video_info->picture_coding_ext.chroma_420_type
                      , video_info->picture_coding_ext.progressive_frame
                      , video_info->picture_coding_ext.composite_display_flag
                      , video_info->picture_coding_ext.v_axis
                      , video_info->picture_coding_ext.field_sequence
                      , video_info->picture_coding_ext.sub_carrier
                      , video_info->picture_coding_ext.burst_amplitude
                      , video_info->picture_coding_ext.sub_carrier_phase );
            break;
        case DETECT_QME :
            mapi_log( LOG_LV2,
                      "        load_intra_quantiser_matrix:%u\n"
                      "        load_non_intra_quantiser_matrix:%u\n"
                      "        load_chroma_intra_quantiser_matrix:%u\n"
                      "        load_chroma_non_intra_quantiser_matrix:%u\n"
                      , video_info->quant_matrix_ext.load_intra_quantiser_matrix
                      , video_info->quant_matrix_ext.load_non_intra_quantiser_matrix
                      , video_info->quant_matrix_ext.load_chroma_intra_quantiser_matrix
                      , video_info->quant_matrix_ext.load_chroma_non_intra_quantiser_matrix );
            break;
        case DETECT_PDE :
            mapi_log( LOG_LV2,
                      "        number_of_frame_centre_offsets:%u\n"
                      "        offsets[0].horizontal:%u\n"
                      "        offsets[0].vertical_offset:%u\n"
                      "        offsets[1].horizontal:%u\n"
                      "        offsets[1].vertical_offset:%u\n"
                      "        offsets[2].horizontal:%u\n"
                      "        offsets[2].vertical_offset:%u\n"
                      , video_info->picture_display_ext.number_of_frame_centre_offsets
                      , video_info->picture_display_ext.frame_centre_offsets[0].horizontal_offset
                      , video_info->picture_display_ext.frame_centre_offsets[0].vertical_offset
                      , video_info->picture_display_ext.frame_centre_offsets[1].horizontal_offset
                      , video_info->picture_display_ext.frame_centre_offsets[1].vertical_offset
                      , video_info->picture_display_ext.frame_centre_offsets[2].horizontal_offset
                      , video_info->picture_display_ext.frame_centre_offsets[2].vertical_offset );
            break;
        case DETECT_PTSE :
            mapi_log( LOG_LV2,
                      "        reference_select_code:%u\n"
                      "        forward_temporal_reference:%d\n"
                      "        backward_temporal_reference:%d\n"
                      , video_info->picture_temporal_scalable_ext.reference_select_code
                      , video_info->picture_temporal_scalable_ext.forward_temporal_reference
                      , video_info->picture_temporal_scalable_ext.backward_temporal_reference );
            break;
        case DETECT_PSSE :
            mapi_log( LOG_LV2,
                      "        lower_layer_temporal_reference:%d\n"
                      "        lower_layer_horizontal_offset:%u\n"
                      "        lower_layer_vertical_offset:%u\n"
                      "        spatial_temporal_weight_code_table_index:%u\n"
                      "        lower_layer_progressive_frame:%u\n"
                      "        lower_layer_deinterlaced_field_select:%u\n"
                      , video_info->picture_spatial_scalable_ext.lower_layer_temporal_reference
                      , video_info->picture_spatial_scalable_ext.lower_layer_horizontal_offset
                      , video_info->picture_spatial_scalable_ext.lower_layer_vertical_offset
                      , video_info->picture_spatial_scalable_ext.spatial_temporal_weight_code_table_index
                      , video_info->picture_spatial_scalable_ext.lower_layer_progressive_frame
                      , video_info->picture_spatial_scalable_ext.lower_layer_deinterlaced_field_select );
            break;
        case DETECT_CPRE :
            mapi_log( LOG_LV2,
                      "        copyright_flag:%u\n"
                      "        copyright_identifier:%u\n"
                      "        original_or_copy:%u\n"
                      "        copyright_number_1:%u\n"
                      "        copyright_number_2:%u\n"
                      "        copyright_number_3:%u\n"
                      , video_info->copyright_ext.copyright_flag
                      , video_info->copyright_ext.copyright_identifier
                      , video_info->copyright_ext.original_or_copy
                      , video_info->copyright_ext.copyright_number_1
                      , video_info->copyright_ext.copyright_number_2
                      , video_info->copyright_ext.copyright_number_3 );
            break;
        case DETECT_SSC :
            mapi_log( LOG_LV2,
                      "[check] detect Slice Start Code.\n" );
            break;
        default :
            break;
    }
}

extern int mpeg_video_judge_start_code
(
    uint8_t                            *start_code_data,
    uint8_t                             identifier,
    mpeg_video_start_code_info_t       *start_code_info
)
{
    int result = -1;
    static const struct {
        mpeg_video_start_code_type             start_code;
        uint32_t                               read_size;
        mpeg_video_start_code_searching_status status;
    } code_list[MPEG_VIDEO_START_CODE_MAX] =
        {
            { MPEG_VIDEO_START_CODE_SHC , MPEG_VIDEO_SEQUENCE_SECTION_HEADER_SIZE , DETECT_SHC  },
            { MPEG_VIDEO_START_CODE_ESC , MPEG_VIDEO_HEADER_EXTENSION_MIN_SIZE    , DETECT_ESC  },
            { MPEG_VIDEO_START_CODE_UDSC, 0                                       , DETECT_UDSC },
            { MPEG_VIDEO_START_CODE_SEC , 0                                       , DETECT_SEC  },
            { MPEG_VIDEO_START_CODE_GSC , MPEG_VIDEO_GOP_SECTION_HEADER_SIZE      , DETECT_GSC  },
            { MPEG_VIDEO_START_CODE_PSC , MPEG_VIDEO_PICTURE_SECTION_HEADER_SIZE  , DETECT_PSC  },
            { MPEG_VIDEO_START_CODE_SSC , MPEG_VIDEO_SLICE_SECTION_HEADER_SIZE    , DETECT_SSC  }
        };
    for( int i = 0; i < MPEG_VIDEO_START_CODE_MAX; ++i )
    {
        if( !code_list[i].read_size || mpeg_video_check_start_code( start_code_data, code_list[i].start_code ) )
            continue;
        mpeg_video_start_code_type             start_code       = code_list[i].start_code;
        uint32_t                               read_size        = code_list[i].read_size;
        mpeg_video_start_code_searching_status searching_status = code_list[i].status;
        if( start_code == MPEG_VIDEO_START_CODE_ESC )
        {
            int extension = mpeg_video_check_extension_start_code_identifier( identifier );
            static const struct {
                mpeg_video_start_code_searching_status searching_status;
                uint32_t                               read_size;
            } extention_type_list[EXTENSION_TYPE_MAX] =
                {
                    { NON_DETECT  , 0                                                   },
                    { DETECT_SESC , MPEG_VIDEO_SEQUENCE_EXTENSION_SIZE                  },
                    { DETECT_SDE  , MPEG_VIDEO_SEQUENCE_DISPLAY_EXTENSION_SIZE          },
                    { DETECT_SSE  , MPEG_VIDEO_SEQUENCE_SCALABLE_EXTENSION_SIZE         },
                    { DETECT_PCESC, MPEG_VIDEO_PICTURE_CODING_EXTENSION_SIZE            },
                    { DETECT_QME  , MPEG_VIDEO_QUANT_MATRIX_EXTENSION_SIZE              },
                    { DETECT_PDE  , MPEG_VIDEO_PICTURE_DISPLAY_EXTENSION_SIZE           },
                    { DETECT_PTSE , MPEG_VIDEO_PICTURE_TEMPORAL_SCALABLE_EXTENSION_SIZE },
                    { DETECT_PSSE , MPEG_VIDEO_PICTURE_SPATIAL_SCALABLE_EXTENSION_SIZE  },
                    { DETECT_CPRE , MPEG_VIDEO_COPYRIGHT_EXTENSION_SIZE                 }
                };
            searching_status = extention_type_list[extension].searching_status;
            read_size        = extention_type_list[extension].read_size;
            if( !read_size )
                continue;
        }
        /* setup. */
        start_code_info->start_code       = start_code;
        start_code_info->read_size        = read_size;
        start_code_info->searching_status = searching_status;
        result = 0;
        break;
    }
    return result;
}

#define FRAME_RATE_CODE_MAX         (9)

extern void mpeg_video_get_frame_rate
(
    mpeg_video_info_t          *video_info,
    uint32_t                   *fps_numerator,
    uint32_t                   *fps_denominator
)
{
    uint8_t frame_rate_code        = video_info->sequence.frame_rate_code;
    uint8_t frame_rate_extension_n = video_info->sequence_ext.frame_rate_extension_n;
    uint8_t frame_rate_extension_d = video_info->sequence_ext.frame_rate_extension_d;
    if( frame_rate_code > FRAME_RATE_CODE_MAX )
        return;
    static const struct {
        uint32_t    fps_num;
        uint32_t    fps_den;
    } frame_rate_code_list[FRAME_RATE_CODE_MAX] =
        {
            {     0,    0 },
            { 24000, 1001 },
            {    24,    1 },
            {    25,    1 },
            { 30000, 1001 },
            {    30,    1 },
            {    50,    1 },
            { 60000, 1001 },
            {    60,    1 }
        };
    *fps_numerator   = frame_rate_code_list[frame_rate_code].fps_num * (frame_rate_extension_n + 1);
    *fps_denominator = frame_rate_code_list[frame_rate_code].fps_den * (frame_rate_extension_d + 1);
}
