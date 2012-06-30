/*****************************************************************************
 * mpeg_stream.c
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
 *   A-4. When you release a modified version to public, you
 *       must publish it with your name.
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

#include "common.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "mpeg_common.h"
#include "mpeg_stream.h"

extern int64_t mpeg_pes_get_timestamp( uint8_t *time_stamp_data )
{
    return (int64_t)(time_stamp_data[0] & 0x0E) << 29
                  |  time_stamp_data[1]         << 22
                  | (time_stamp_data[2] & 0xFE) << 14
                  |  time_stamp_data[3]         << 7
                  | (time_stamp_data[4] & 0xFE) >> 1;
}

extern int mpeg_pes_check_start_code( uint8_t *start_code, mpeg_pes_packet_start_code_type start_code_type )
{
    static const uint8_t pes_start_code_common_head[PES_PACKET_START_CODE_SIZE - 1] = { 0x00, 0x00, 0x01 };
    static const struct {
        uint8_t     mask;
        uint8_t     code;
    } pes_stream_id_list[PES_PACKET_START_CODE_MAX] =
        {
            /* MPEG-1/2 stream type. */
            { 0xBD, 0xBD },         /* Private Stream 1             */
            { 0xBE, 0xBE },         /* Padding Stream               */
            { 0xBF, 0xBF },         /* Private Stream 2             */
            { 0xF0, 0xE0 },         /* Video Stream                 */
            { 0xE0, 0xC0 },         /* Audio Stream                 */
            { 0xF3, 0xF3 },         /* MHEG Reserved                */
            /* MPEG-2 stream type. */
            { 0xBC, 0xBC },         /* Program Stream Map           */
            { 0xF0, 0xF0 },         /* License Management Message 1 */
            { 0xF1, 0xF1 },         /* License Management Message 2 */
            { 0xF2, 0xF2 },         /* DSM Control Command          */
            { 0xF4, 0xFC },         /* ITU-T Reserved               */
            { 0xF8, 0xF8 },         /* ITU-T Reserved               */
            { 0xF9, 0xF9 },         /* PS Trasnport on TS           */
            { 0xFF, 0xFF },         /* Program Stream Directory     */
            /* User Private */
            { 0xFF, 0xE2 },         /* MPEG-4 AVC Stream            */
            { 0xFF, 0x0F },         /* VC-1 Video Stream 1          */
            { 0xFF, 0x0D },         /* VC-1 Video Stream 2          */
            { 0xFF, 0xFD }          /* AC-3/DTS Audio Stream        */
        };
    if( memcmp( start_code, pes_start_code_common_head, PES_PACKET_START_CODE_SIZE - 1 ) )
        return -1;
    if( (start_code[PES_PACKET_START_CODE_SIZE - 1] & pes_stream_id_list[start_code_type].mask) != pes_stream_id_list[start_code_type].code )
        return -1;
    return 0;
}

extern void mpeg_pes_get_header_info( uint8_t *buf, mpeg_pes_header_info_t *pes_info )
{
    pes_info->packet_length             = ((buf[0] << 8) | buf[1]);
    /* reserved '10'    2bit            = (buf[2] & 0xC0) >> 6; */
    pes_info->scrambe_control           = (buf[2] & 0x30) >> 4;
    pes_info->priority                  = !!(buf[2] & 0x08);
    pes_info->data_alignment            = !!(buf[2] & 0x04);
    pes_info->copyright                 = !!(buf[2] & 0x02);
    pes_info->original_or_copy          = !!(buf[2] & 0x01);
    pes_info->pts_flag                  = !!(buf[3] & 0x80);
    pes_info->dts_flag                  = !!(buf[3] & 0x40);
    pes_info->escr_flag                 = !!(buf[3] & 0x20);
    pes_info->es_rate_flag              = !!(buf[3] & 0x10);
    pes_info->dsm_trick_mode_flag       = !!(buf[3] & 0x08);
    pes_info->additional_copy_info_flag = !!(buf[3] & 0x04);
    pes_info->crc_flag                  = !!(buf[3] & 0x02);
    pes_info->extention_flag            = !!(buf[3] & 0x01);
    pes_info->header_length             = buf[4];
}

extern mpeg_pes_packet_start_code_type mpeg_pes_get_stream_start_code( mpeg_stream_group_type stream_judge )
{
    mpeg_pes_packet_start_code_type start_code = PES_PACKET_START_CODE_INVALID;
    if( stream_judge & STREAM_IS_VIDEO )
        start_code = PES_PACKET_START_CODE_VIDEO_STREAM;
    else if( stream_judge & STREAM_IS_AUDIO )
        start_code = PES_PACKET_START_CODE_AUDIO_STREAM;
    /* check User Private. */
    switch( stream_judge )          // FIXME
    {
        case STREAM_IS_PCM_AUDIO :
            start_code = PES_PACKET_START_CODE_PRIVATE_STREAM_1;
            break;
        case STREAM_IS_DOLBY_AUDIO :
            start_code = PES_PACKET_START_CODE_AC3_DTS_AUDIO_STREAM;
            break;
        default :
            break;
    }
    return start_code;
}

#define READ_DESCRIPTOR( name )         static void read_##name##_descriptor( uint8_t *descriptor, name##_descriptor_info_t *name )

READ_DESCRIPTOR( video_stream )
{
    video_stream->multiple_frame_rate_flag         = !!(descriptor[2] & 0x80);
    video_stream->frame_rate_code                  =   (descriptor[2] & 0x78) >> 3;
    video_stream->MPEG_1_only_flag                 = !!(descriptor[2] & 0x04);
    video_stream->constrained_parameter_flag       = !!(descriptor[2] & 0x02);
    video_stream->still_picture_flag               =    descriptor[2] & 0x01;
    if( video_stream->MPEG_1_only_flag )
    {
        video_stream->profile_and_level_indication =    descriptor[3];
        video_stream->chroma_format                =   (descriptor[4] & 0xC0) >> 6;
        video_stream->frame_rate_extension_flag    = !!(descriptor[4] & 0x20);
    }
    else
    {
        video_stream->profile_and_level_indication = 0;
        video_stream->chroma_format                = 0;
        video_stream->frame_rate_extension_flag    = 0;
    }
}

READ_DESCRIPTOR( audio_stream )
{
    audio_stream->free_format_flag              = !!(descriptor[2] & 0x80);
    audio_stream->id                            = !!(descriptor[2] & 0x40);
    audio_stream->layer                         =   (descriptor[2] & 0x30) >> 4;
    audio_stream->variable_rate_audio_indicator = !!(descriptor[2] & 0x08);
}

READ_DESCRIPTOR( hierarchy )
{
    /* reserved     4bit                      = (descriptor[2] & 0xF0) >> 4; */
    hierarchy->hierarchy_type                 =  descriptor[2] & 0x0F;
    /* reserved     2bit                      = (descriptor[3] & 0xC0) >> 6; */
    hierarchy->hierarchy_layer_index          =  descriptor[3] & 0x3F;
    /* reserved     2bit                      = (descriptor[4] & 0xC0) >> 6; */
    hierarchy->hierarchy_embedded_layer_index =  descriptor[4] & 0x3F;
    /* reserved     2bit                      = (descriptor[5] & 0xC0) >> 6; */
    hierarchy->hierarchy_channel              =  descriptor[5] & 0x3F;
}

READ_DESCRIPTOR( registration )
{
    registration->format_identifier              = (descriptor[2] << 24) | (descriptor[3] << 16) | (descriptor[4] << 8) | descriptor[5];
    /* additional_identification_info */
    registration->additional_identification_info =  descriptor;        // FIXME
}

READ_DESCRIPTOR( data_stream_alignment )
{
    data_stream_alignment->alignment_type = descriptor[2];
}

READ_DESCRIPTOR( target_background_grid )
{
    target_background_grid->horizontal_size          =  (descriptor[2] << 6) | (descriptor[3] >> 2);
    target_background_grid->vertical_size            = ((descriptor[3] & 0x02) << 12) | (descriptor[4] << 4) | (descriptor[5] >> 4);
    target_background_grid->aspect_ratio_information =   descriptor[5] & 0x04;
}

READ_DESCRIPTOR( video_window )
{
    video_window->horizontal_offset = 0;
    video_window->vertical_offset   = 0;
    video_window->window_priority   = 0;
}

READ_DESCRIPTOR( conditional_access )
{
    conditional_access->CA_system_ID =  (descriptor[2] << 8) | descriptor[3];
    /* reserved     3bit             =  (descriptor[4] & 0x30) >> 5; */
    conditional_access->CA_PID       = ((descriptor[4] & 0x1F) << 8) | descriptor[5];
#if 0
    for( uint8_t i = 6; i < descriptor[1] + 2; ++i )
    {
        /* private_data_byte        8bit * N    */
        uint8_t private_data_byte    = descriptor[i];
    }
#endif
}

READ_DESCRIPTOR( ISO_639_language )
{
    ISO_639_language->data_num                          =  descriptor[1];
    for( uint8_t i = 0; i < ISO_639_language->data_num; ++i )
    {
        uint8_t index = 2+i;
        ISO_639_language->data[i].ISO_639_language_code = (descriptor[index+0] << 16) | (descriptor[index+1] << 8) | descriptor[index+2];
        ISO_639_language->data[i].audio_type            =  descriptor[index+3];
    }
}

READ_DESCRIPTOR( system_clock )
{
    system_clock->external_clock_reference_indicator = !!(descriptor[2] & 0x80);
    /* reserved     1bit                             = !!(descriptor[2] & 0x40); */
    system_clock->clock_accuracy_integer             =    descriptor[2] & 0x3F;
    system_clock->clock_accuracy_exponent            =    descriptor[3] >> 5;
}

READ_DESCRIPTOR( multiplex_buffer_utilization )
{
    multiplex_buffer_utilization->bound_valid_flag       = !!(descriptor[2] & 0x80);
    multiplex_buffer_utilization->LTW_offset_lower_bound =    descriptor[2] & 0x7F;
    /* reserved     1bit                                 = !!(descriptor[3] & 0x80); */
    multiplex_buffer_utilization->LTW_offset_upper_bound =   (descriptor[3] & 0x7E) >> 1;
}

READ_DESCRIPTOR( copyright )
{
    copyright->copyright_identifier       = descriptor[2];
#if 0
    for( uint8_t i = 3; i < descriptor[1] + 2; ++i )
    {
        /* additional_copyright_info        8bit * N    */
        uint8_t additional_copyright_info = descriptor[i];
    }
#endif
}

READ_DESCRIPTOR( maximum_bitrate )
{
    /* reserved     2bit             =  (descriptor[2] & 0xC0) >> 6; */
    maximum_bitrate->maximum_bitrate = ((descriptor[2] & 0x3F) << 16) | (descriptor[3] << 8) | descriptor[4];
}

READ_DESCRIPTOR( private_data_indicator )
{
    private_data_indicator->private_data_indicator = (descriptor[2] << 24) | (descriptor[3] << 16) | (descriptor[4] << 8) | descriptor[5];
}

READ_DESCRIPTOR( smoothing_buffer )
{
    /* reserved     2bit           =  (descriptor[2] & 0xC0) >> 6; */
    smoothing_buffer->sb_leak_rate = ((descriptor[2] & 0x3F) << 16) | (descriptor[3] << 8) | descriptor[4];
    /* reserved     2bit           =  (descriptor[5] & 0xC0) >> 6; */
    smoothing_buffer->sb_size      = ((descriptor[5] & 0x3F) << 16) | (descriptor[6] << 8) | descriptor[7];
}

READ_DESCRIPTOR( STD )
{
    /* reserved     7bit = descriptor[2] >> 1; */
    STD->leak_valid_flag = descriptor[2] & 0x01;
}

READ_DESCRIPTOR( ibp )
{
    ibp->closed_gop_flag    = !!(descriptor[2] & 0x80);
    ibp->identical_gop_flag = !!(descriptor[2] & 0x40);
    ibp->max_gop_length     =  ((descriptor[2] & 0x3F) << 8) | descriptor[3];
}

READ_DESCRIPTOR( MPEG4_video )
{
    MPEG4_video->MPEG4_visual_profile_and_level = descriptor[2];
}

READ_DESCRIPTOR( MPEG4_audio )
{
    MPEG4_audio->MPEG4_audio_profile_and_level = descriptor[2];
}

READ_DESCRIPTOR( IOD )
{
    IOD->Scope_of_IOD_label      = descriptor[2];
    IOD->IOD_label               = descriptor[3];
    /* InitialObjectDescriptor()        ISO/IEC 14496-1 */
    IOD->InitialObjectDescriptor = descriptor[4];        // FIXME
}

READ_DESCRIPTOR( SL )
{
    SL->ES_ID = (descriptor[2] << 8) | descriptor[3];
}

READ_DESCRIPTOR( FMC )
{
    uint8_t i;
    for( i = 0; i * 3 < descriptor[1]; ++i )
    {
        uint8_t index = 2 + i * 3;
        FMC->data[i].ES_ID          = (descriptor[index+0] << 8) | descriptor[index+1];
        FMC->data[i].FlexMuxChannel =  descriptor[index+2];
    }
    FMC->data_num = i;
}

READ_DESCRIPTOR( External_ES_ID )
{
    External_ES_ID->External_ES_ID = (descriptor[2] << 8) | descriptor[3];
}

READ_DESCRIPTOR( MuxCode )
{
    /* MuxCodeTableEntry()      ISO/IEC 14496-1 */
    MuxCode->MuxCodeTableEntry = descriptor;        // FIXME
}

READ_DESCRIPTOR( FmxBufferSize )
{
    /* FlexMuxBufferDescriptor()            ISO/IEC 14496-1 */
    /* DefaultFlexMuxBufferDescriptor()     ISO/IEC 14496-1 */
    FmxBufferSize->DefaultFlexMuxBufferDescriptor = descriptor;     // FIXME
    FmxBufferSize->FlexMuxBufferDescriptor        = descriptor;     // FIXME
}

READ_DESCRIPTOR( MultiplexBuffer )
{
    MultiplexBuffer->MB_buffer_size = (descriptor[2] << 16) | (descriptor[3] << 8) | descriptor[4];
    MultiplexBuffer->TB_leak_rate   = (descriptor[5] << 16) | (descriptor[6] << 8) | descriptor[7];
}

#undef READ_DESCRIPTOR

#define EXECUTE_READ_DESCRIPTOR( name )                                     \
{                                                                           \
    case name##_descriptor :                                                \
        read_##name##_descriptor( descriptor, &(descriptor_info->name) );   \
        break;                                                              \
}
extern void mpeg_stream_get_descriptor_info( mpeg_stream_type stream_type, uint8_t *descriptor, mpeg_descriptor_info_t *descriptor_info )
{
    descriptor_info->tag    = descriptor[0];
    descriptor_info->length = descriptor[1];
    switch( descriptor_info->tag )
    {
        EXECUTE_READ_DESCRIPTOR( video_stream )
        EXECUTE_READ_DESCRIPTOR( audio_stream )
        EXECUTE_READ_DESCRIPTOR( hierarchy )
        EXECUTE_READ_DESCRIPTOR( registration )
        EXECUTE_READ_DESCRIPTOR( data_stream_alignment )
        EXECUTE_READ_DESCRIPTOR( target_background_grid )
        EXECUTE_READ_DESCRIPTOR( video_window )
        EXECUTE_READ_DESCRIPTOR( conditional_access )
        EXECUTE_READ_DESCRIPTOR( ISO_639_language )
        EXECUTE_READ_DESCRIPTOR( system_clock )
        EXECUTE_READ_DESCRIPTOR( multiplex_buffer_utilization )
        EXECUTE_READ_DESCRIPTOR( copyright )
        EXECUTE_READ_DESCRIPTOR( maximum_bitrate )
        EXECUTE_READ_DESCRIPTOR( private_data_indicator )
        EXECUTE_READ_DESCRIPTOR( smoothing_buffer )
        EXECUTE_READ_DESCRIPTOR( STD )
        EXECUTE_READ_DESCRIPTOR( ibp )
        EXECUTE_READ_DESCRIPTOR( MPEG4_video )
        EXECUTE_READ_DESCRIPTOR( MPEG4_audio )
        EXECUTE_READ_DESCRIPTOR( IOD )
        EXECUTE_READ_DESCRIPTOR( SL )
        EXECUTE_READ_DESCRIPTOR( FMC )
        EXECUTE_READ_DESCRIPTOR( External_ES_ID )
        EXECUTE_READ_DESCRIPTOR( MuxCode )
        EXECUTE_READ_DESCRIPTOR( FmxBufferSize )
        EXECUTE_READ_DESCRIPTOR( MultiplexBuffer )
        default :
            break;
    }
}
#undef EXECUTE_READ_DESCRIPTOR

#define PRINT_DESCRIPTOR_INFO( name, ... )          \
{                                                   \
    case name##_descriptor :                        \
        dprintf( LOG_LV2,                           \
                 "[check] "#name"_descriptor\n"     \
                 __VA_ARGS__ );                     \
}
extern void mpeg_stream_debug_descriptor_info( mpeg_descriptor_info_t *descriptor_info )
{
    switch( descriptor_info->tag )
    {
        PRINT_DESCRIPTOR_INFO( video_stream,
                "        multiple_frame_rate_flag:%u\n"
                "        frame_rate_code:%u\n"
                "        MPEG_1_only_flag:%u\n"
                "        constrained_parameter_flag:%u\n"
                "        still_picture_flag:%u\n"
                "        profile_and_level_indication:%u\n"
                "        chroma_format:%u\n"
                "        profile_and_level_indication:%u\n"
                "        frame_rate_extension_flag:%u\n"
                , descriptor_info->video_stream.multiple_frame_rate_flag
                , descriptor_info->video_stream.frame_rate_code
                , descriptor_info->video_stream.MPEG_1_only_flag
                , descriptor_info->video_stream.constrained_parameter_flag
                , descriptor_info->video_stream.still_picture_flag
                , descriptor_info->video_stream.profile_and_level_indication
                , descriptor_info->video_stream.chroma_format
                , descriptor_info->video_stream.profile_and_level_indication
                , descriptor_info->video_stream.frame_rate_extension_flag
            )
            break;
        PRINT_DESCRIPTOR_INFO( audio_stream,
                "        free_format_flag:%u\n"
                "        id:%u\n"
                "        layer:%u\n"
                "        variable_rate_audio_indicator:%u\n"
                , descriptor_info->audio_stream.free_format_flag
                , descriptor_info->audio_stream.id
                , descriptor_info->audio_stream.layer
                , descriptor_info->audio_stream.variable_rate_audio_indicator
            )
            break;
        PRINT_DESCRIPTOR_INFO( hierarchy,
                "        hierarchy_type:%u\n"
                "        hierarchy_layer_index:%u\n"
                "        hierarchy_embedded_layer_index:%u\n"
                "        hierarchy_channel:%u\n"
                , descriptor_info->hierarchy.hierarchy_type
                , descriptor_info->hierarchy.hierarchy_layer_index
                , descriptor_info->hierarchy.hierarchy_embedded_layer_index
                , descriptor_info->hierarchy.hierarchy_channel
            )
            break;
        PRINT_DESCRIPTOR_INFO( registration,
                "        format_identifier:0x%08x\n"
                , descriptor_info->registration.format_identifier
            )
            /* additional_identification_info */        // FIXME
            if( descriptor_info->length - 4 > 0 )
            {
                dprintf( LOG_LV2, "        additional_info:" );
                for( uint8_t i = 6; i < descriptor_info->length; ++i )
                    dprintf( LOG_LV2, " 0x%02X", descriptor_info->registration.additional_identification_info[i] );
                dprintf( LOG_LV2, "\n" );
            }
            break;
        PRINT_DESCRIPTOR_INFO( data_stream_alignment,
                "        alignment_type:%u\n"
                , descriptor_info->data_stream_alignment.alignment_type
            )
            break;
        PRINT_DESCRIPTOR_INFO( target_background_grid,
                "        horizontal_size:%u\n"
                "        vertical_size:%u\n"
                "        aspect_ratio_information:%u\n"
                , descriptor_info->target_background_grid.horizontal_size
                , descriptor_info->target_background_grid.vertical_size
                , descriptor_info->target_background_grid.aspect_ratio_information
            )
            break;
        PRINT_DESCRIPTOR_INFO( video_window,
                "        horizontal_offset:%u\n"
                "        vertical_offset:%u\n"
                "        window_priority:%u\n"
                , descriptor_info->video_window.horizontal_offset
                , descriptor_info->video_window.vertical_offset
                , descriptor_info->video_window.window_priority
            )
            break;
        PRINT_DESCRIPTOR_INFO( conditional_access,
                "        CA_system_ID:%u\n"
                "        CA_PID:%u\n"
                , descriptor_info->conditional_access.CA_system_ID
                , descriptor_info->conditional_access.CA_PID
            )
            break;
        PRINT_DESCRIPTOR_INFO( ISO_639_language )
            for( uint8_t i = 0; i < descriptor_info->ISO_639_language.data_num; ++i )
                dprintf( LOG_LV2,
                         "        ISO_639_language_code:0x%06X\n"
                         "        audio_type:%u\n"
                         , descriptor_info->ISO_639_language.data[i].ISO_639_language_code
                         , descriptor_info->ISO_639_language.data[i].audio_type );
            break;
        PRINT_DESCRIPTOR_INFO( system_clock,
                "        external_clock_reference_indicator:%u\n"
                "        clock_accuracy_integer:%u\n"
                "        clock_accuracy_exponent:%u\n"
                , descriptor_info->system_clock.external_clock_reference_indicator
                , descriptor_info->system_clock.clock_accuracy_integer
                , descriptor_info->system_clock.clock_accuracy_exponent
            )
            break;
        PRINT_DESCRIPTOR_INFO( multiplex_buffer_utilization,
                "        bound_valid_flag:%u\n"
                "        LTW_offset_lower_bound:%u\n"
                "        LTW_offset_upper_bound:%u\n"
                , descriptor_info->multiplex_buffer_utilization.bound_valid_flag
                , descriptor_info->multiplex_buffer_utilization.LTW_offset_lower_bound
                , descriptor_info->multiplex_buffer_utilization.LTW_offset_upper_bound
            )
            break;
        PRINT_DESCRIPTOR_INFO( copyright,
                "        copyright_identifier:%u\n"
                , descriptor_info->copyright.copyright_identifier
            )
            break;
        PRINT_DESCRIPTOR_INFO( maximum_bitrate,
                "        maximum_bitrate:%u\n"
                , descriptor_info->maximum_bitrate.maximum_bitrate
            )
            break;
        PRINT_DESCRIPTOR_INFO( private_data_indicator,
                "        private_data_indicator:%u\n"
                , descriptor_info->private_data_indicator.private_data_indicator
            )
            break;
        PRINT_DESCRIPTOR_INFO( smoothing_buffer,
                "        sb_leak_rate:%u\n"
                "        sb_size:%u\n"
                , descriptor_info->smoothing_buffer.sb_leak_rate
                , descriptor_info->smoothing_buffer.sb_size
            )
            break;
        PRINT_DESCRIPTOR_INFO( STD,
                "        leak_valid_flag:%u\n"
                , descriptor_info->STD.leak_valid_flag
            )
            break;
        PRINT_DESCRIPTOR_INFO( ibp,
                "        closed_gop_flag:%u\n"
                "        identical_gop_flag:%u\n"
                "        max_gop_length:%u\n"
                , descriptor_info->ibp.closed_gop_flag
                , descriptor_info->ibp.identical_gop_flag
                , descriptor_info->ibp.max_gop_length
            )
            break;
        PRINT_DESCRIPTOR_INFO( MPEG4_video,
                "        MPEG4_visual_profile_and_level:%u\n"
                , descriptor_info->MPEG4_video.MPEG4_visual_profile_and_level
            )
            break;
        PRINT_DESCRIPTOR_INFO( MPEG4_audio,
                "        MPEG4_audio_profile_and_level:%u\n"
                , descriptor_info->MPEG4_audio.MPEG4_audio_profile_and_level
            )
            break;
        PRINT_DESCRIPTOR_INFO( IOD,
                "        Scope_of_IOD_label:%u\n"
                "        IOD_label:%u\n"
                "        InitialObjectDescriptor: 0x%02X\n"
                , descriptor_info->IOD.Scope_of_IOD_label
                , descriptor_info->IOD.IOD_label
                , descriptor_info->IOD.InitialObjectDescriptor      // FIXME
            )
            break;
        PRINT_DESCRIPTOR_INFO( SL,
                "        ES_ID:%u\n"
                , descriptor_info->SL.ES_ID
            )
            break;
        PRINT_DESCRIPTOR_INFO( FMC )
            for( uint8_t i = 0; i < descriptor_info->FMC.data_num; ++i )
                dprintf( LOG_LV2,
                         "        ES_ID:%u\n"
                         "        FlexMuxChannel:%u\n"
                         , descriptor_info->FMC.data[i].ES_ID
                         , descriptor_info->FMC.data[i].FlexMuxChannel );
            break;
        PRINT_DESCRIPTOR_INFO( External_ES_ID,
                "        External_ES_ID:%u\n"
                , descriptor_info->External_ES_ID.External_ES_ID
            )
            break;
        PRINT_DESCRIPTOR_INFO( MuxCode )            // FIXME
            break;
        PRINT_DESCRIPTOR_INFO( FmxBufferSize )      // FIXME
            break;
        PRINT_DESCRIPTOR_INFO( MultiplexBuffer,
                "        MB_buffer_size:%u\n"
                "        TB_leak_rate:%u\n"
                , descriptor_info->MultiplexBuffer.MB_buffer_size
                , descriptor_info->MultiplexBuffer.TB_leak_rate
            )
            break;
        default :
            break;
    }
}
#undef PRINT_DESCRIPTOR_INFO

extern mpeg_stream_group_type mpeg_stream_judge_type( mpeg_stream_type stream_type, uint8_t *descriptor_tags, uint16_t descriptor_num )
{
    mpeg_stream_group_type stream_judge = STREAM_IS_UNKNOWN;
    switch( stream_type )
    {
        case STREAM_VIDEO_MPEG1 :
            stream_judge = STREAM_IS_MPEG1_VIDEO;
            break;
        case STREAM_VIDEO_MPEG2 :
            stream_judge = STREAM_IS_MPEG2_VIDEO;
            break;
        case STREAM_VIDEO_MPEG2_A :
        case STREAM_VIDEO_MPEG2_B :
        case STREAM_VIDEO_MPEG2_C :
        case STREAM_VIDEO_MPEG2_D :
            break;
        case STREAM_VIDEO_MP4 :
        case STREAM_VIDEO_AVC :
            stream_judge = STREAM_IS_MPEG4_VIDEO;
            break;
        case STREAM_AUDIO_MP1 :
            stream_judge = STREAM_IS_MPEG1_AUDIO;
            break;
        case STREAM_AUDIO_MP2 :
            stream_judge = STREAM_IS_MPEG2_AUDIO;
            break;
        case STREAM_AUDIO_AAC :
            stream_judge = STREAM_IS_AAC_AUDIO;
            break;
        //case STREAM_VIDEO_PRIVATE :
        case STREAM_AUDIO_LPCM :
            for( uint16_t i = 0; i < descriptor_num; ++i )
            {
                if( descriptor_tags[i] == video_stream_descriptor )
                    stream_judge = STREAM_IS_PRIVATE_VIDEO;
                else if( descriptor_tags[i] == registration_descriptor )
                    stream_judge = STREAM_IS_PCM_AUDIO;
                else
                    continue;
                break;
            }
            break;
        //case STREAM_AUDIO_AC3_DTS :
        case STREAM_AUDIO_AC3 :
        case STREAM_AUDIO_DTS :
        case STREAM_AUDIO_MLP :
        case STREAM_AUDIO_DDPLUS :
        case STREAM_AUDIO_DTS_HD :
        case STREAM_AUDIO_DTS_HD_XLL :
        case STREAM_AUDIO_DDPLUS_SUB :
        case STREAM_AUDIO_DTS_HD_SUB :
            stream_judge = STREAM_IS_DOLBY_AUDIO;       // FIXME
            break;
        default :
            break;
    }
    return stream_judge;
}

typedef int (*header_check_func)( uint8_t *header );

static int mpa_header_check( uint8_t *header )
{
    if( header[0] != 0xFF || (header[1] & 0xE0) != 0xE0 )
        return -1;
    int version_id = (header[1] & 0x18) >> 3;
    if( version_id == 0x01 )                    /* MPEG Audio version ID : '01' reserved            */
        return -1;
    int layer = (header[1] & 0x06) >> 1;
    if( !layer )                                /* layer : '00' reserved                            */
        return -1;
    /* protection_bit = header[1] & 0x01);              */
    int bitrate_index = header[2] >> 4;
    if( bitrate_index == 0x0F )                 /* bitrate index : '1111' bad                       */
        return -1;
    int sampling_frequency_index = (header[2] & 0x0C) >> 2;
    if( sampling_frequency_index == 0x03 )      /* Sampling rate frequency index : '11' reserved    */
        return -1;
    uint8_t channel_mode = (header[3] & 0x60) >> 5;
    /* check bitrate & channel_mode matrix. */
    enum {
        MPEG25 = 0x01,
        MPEG2  = 0x02,
        MPEG1  = 0x03,
    };
    enum {
        LAYER3 = 0x01,
        LAYER2 = 0x02,
        LAYER1 = 0x03,
    };
    enum {
        STEREO         = 0x00,
        JOINT_STEREO   = 0x01,
        DUAL_CHANNEL   = 0x02,
        SINGLE_CHANNEL = 0x03
    };
    static const uint8_t version_layer_matrix[3][3] =
        {
            {  4, 4, 3 },
            {  4, 4, 3 },
            {  2, 1, 0 }
        };
    static const uint16_t bitrate_matrix[5][15] =
        {
            {   0,   32,  64,  96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
            {   0,   32,  48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384 },
            {   0,   32,  40,  48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320 },
            {   0,   32,  48,  56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256 },
            {   0,    8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160 }
        };
    int vl_index = version_layer_matrix[version_id - 1][layer - 1];
    uint16_t bitrate = bitrate_matrix[vl_index][bitrate_index];
    static const uint16_t ng_matrix[2][4] =
        {
            {  32,  48,  56,  80 },             /* stereo / joint stereo / dual channel */
            { 224, 256, 320, 384 }              /* single channel                       */
        };
    int index = (channel_mode == SINGLE_CHANNEL);
    if( bitrate == ng_matrix[index][0] || bitrate == ng_matrix[index][1]
     || bitrate == ng_matrix[index][2] || bitrate == ng_matrix[index][3] )
        return -1;
    /* detect header. */
    dprintf( LOG_LV4, "[debug] [MPEG-Audio] detect header. \n" );
    return 0;
}

#include <math.h>
static int aac_header_check( uint8_t *header )
{
    if( header[0] != 0xFF || (header[1] & 0xF0) != 0xF0 )
        return -1;
    if( header[1] & 0x06 )                      /* layer : allways 0                                        */
        return -1;
    if( (header[2] & 0xC0) == 0xC0 )            /* profile : '11' reserved                                  */
        return -1;
    int sampling_frequency_index = (header[2] & 0x3C) >> 2;
    if( sampling_frequency_index > 11 )         /* sampling_frequency_index : 0xc-0xf reserved              */
        return -1;
    uint32_t aac_frame_length = ((header[3] & 0x03) << 11) | (header[4] << 3) | (header[5] >> 5);
    if( !aac_frame_length )                     /* aac frame length                             */
        return -1;
    /* protection absent          =   header[1] & 0x01;             */
    /* uint8_t channel_configuration = ((header[2] & 0x01) << 2) | (header[3] >> 6); */
    /* detect header. */
    dprintf( LOG_LV4, "[debug] [ADTS-AAC] detect header. aac_frame_len:%d\n", aac_frame_length );
    return 0;
}

extern int32_t mpeg_stream_check_header( mpeg_stream_type stream_type, mpeg_stream_group_type stream_judge, int search_point, uint8_t *buffer, uint32_t buffer_size )
{
    int32_t header_offset = -1;
    switch( stream_judge )
    {
        case STREAM_IS_MPEG1_AUDIO :
        case STREAM_IS_MPEG2_AUDIO :
        case STREAM_IS_AAC_AUDIO :
            {
                static const struct {
                    header_check_func   func;
                    int                 size;
                } header_check[2] =
                    {
                        { mpa_header_check, STREAM_MPA_HEADER_CHECK_SIZE },
                        { aac_header_check, STREAM_AAC_HEADER_CHECK_SIZE }
                    };
                int index = (stream_judge == STREAM_IS_AAC_AUDIO);
                int idx_max = buffer_size - header_check[index].size;
                for( int i = 0; i <= idx_max; ++i )
                {
                    if( header_check[index].func( &(buffer[i]) ) )
                        continue;
                    /* setup. */
                    header_offset = i;
                    dprintf( LOG_LV4, "        check_buffer_size:%d  header_offset:%d\n", buffer_size, header_offset );
                    break;
                }
            }
            break;
        case STREAM_IS_PCM_AUDIO :      // FIXME
            if( buffer_size > STREAM_LPCM_HEADER_CHECK_SIZE )
            {
#if 0
                int channels_num_code = (buffer[2] & 0xF0) > 4;
                int sample_rate_code  =  buffer[2] & 0x0F;
                int sample_size_code  = (buffer[3] & 0xC0) > 6;
#endif
                /* setup. */
                /* 0 (start point) :  skip header size.             */
                /* 1 ( end  point) :  return end point.             */
                header_offset = search_point ? 0 : STREAM_LPCM_HEADER_CHECK_SIZE;
                dprintf( LOG_LV4, "[debug] [LPCM] detect header.\n"
                                  "        check_buffer_size:%d  header_offset:%d\n", buffer_size, header_offset );
                break;
            }
            break;
        case STREAM_IS_DOLBY_AUDIO :    // FIXME
            header_offset = 0;          // FIXME
            break;
        default :
            header_offset = 0;
            break;
    }
    return header_offset;
}

extern uint32_t mpeg_stream_get_header_check_size( mpeg_stream_group_type stream_judge )
{
    uint32_t check_size = STREAM_HEADER_CHECK_MAX_SIZE;
    switch( stream_judge )
    {
        case STREAM_IS_MPEG1_AUDIO :
        case STREAM_IS_MPEG2_AUDIO :
            check_size = STREAM_MPA_HEADER_CHECK_SIZE;
            break;
        case STREAM_IS_AAC_AUDIO :
            check_size = STREAM_AAC_HEADER_CHECK_SIZE;
            break;
        case STREAM_IS_PCM_AUDIO :
            check_size = STREAM_LPCM_HEADER_CHECK_SIZE;
            break;
        case STREAM_IS_DOLBY_AUDIO :
            break;
        default :
            break;
    }
    return check_size;
}
