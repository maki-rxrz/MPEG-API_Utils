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
#include "mpeg_stream.h"

extern int64_t mpeg_pes_get_timestamp( uint8_t *time_stamp_data )
{
    return (int64_t)(time_stamp_data[0] & 0x0E) << 29
                  |  time_stamp_data[1]         << 22
                  | (time_stamp_data[2] & 0xFE) << 14
                  |  time_stamp_data[3]         << 7
                  | (time_stamp_data[4] & 0xFE) >> 1;
}

extern int mpeg_pes_check_steam_id_type( uint8_t *start_code, mpeg_pes_stream_id_type stream_id_type )
{
    static const uint8_t pes_steam_id_common_head[PES_PACKET_START_CODE_SIZE - 1] = { 0x00, 0x00, 0x01 };
    static const uint8_t pes_steam_ids[PES_STEAM_ID_TYPE_MAX] =
        {
            0xBC,       /* program_stream_map                                                           */
            0xBD,       /* private_stream_1                                                             */
            0xBE,       /* padding_stream                                                               */
            0xBF,       /* private_stream_2                                                             */
            0xC0,       /* ISO/IEC xxxxx-x audio stream                                                 */
            0xE0,       /* Rec. ITU-T H.26x | ISO/IEC xxxxx-x video stream                              */
            0xF0,       /* ECM_stream                                                                   */
            0xF1,       /* EMM_stream                                                                   */
            0xF2,       /* Rec. ITU-T H.222.0 | ISO/IEC 13818-1 Annex A or ISO/IEC 13818-6_DSMCC_stream */
            0xF3,       /* ISO/IEC_13522_stream                                                         */
            0xF4,       /* Rec. ITU-T H.222.1 type A                                                    */
            0xF5,       /* Rec. ITU-T H.222.1 type B                                                    */
            0xF6,       /* Rec. ITU-T H.222.1 type C                                                    */
            0xF7,       /* Rec. ITU-T H.222.1 type D                                                    */
            0xF8,       /* Rec. ITU-T H.222.1 type E                                                    */
            0xF9,       /* ancillary_stream                                                             */
            0xFA,       /* ISO/IEC 14496-1_SL-packetized_stream                                         */
            0xFB,       /* ISO/IEC 14496-1_FlexMux_stream                                               */
            0xFC,       /* metadata stream                                                              */
            0xFD,       /* extended_stream_id                                                           */
            0xFE,       /* reserved data stream                                                         */
            0xFF,       /* program_stream_directory                                                     */
        };
    uint8_t mask = (stream_id_type == PES_STEAM_ID_VIDEO_STREAM) ? 0xF0
                 : (stream_id_type == PES_STEAM_ID_AUDIO_STREAM) ? 0xE0
                 :                                                 0xFF;
    if( memcmp( start_code, pes_steam_id_common_head, PES_PACKET_START_CODE_SIZE - 1 ) )
        return -1;
    if( (start_code[PES_PACKET_START_CODE_SIZE - 1] & mask) != pes_steam_ids[stream_id_type] )
        return -1;
    return 0;
}

extern void mpeg_pes_get_header_info( uint8_t *buf, mpeg_pes_header_info_t *pes_info )
{
    pes_info->packet_length             =  ((buf[0] << 8) | buf[1]);
    /* reserved '10'    2bit            =   (buf[2] & 0xC0) >> 6;   */
    pes_info->scrambe_control           =   (buf[2] & 0x30) >> 4;
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
    pes_info->extension_flag            = !!(buf[3] & 0x01);
    pes_info->header_length             =    buf[4];
}

extern mpeg_pes_stream_id_type mpeg_pes_get_steam_id_type( mpeg_stream_group_type stream_judge )
{
    mpeg_pes_stream_id_type stream_id_type = PES_STEAM_ID_INVALID;
    switch( stream_judge )
    {
        case STREAM_IS_PRIVATE_VIDEO :
        case STREAM_IS_PCM_AUDIO :
            stream_id_type = PES_STEAM_ID_PRIVATE_STREAM_1;
            break;
        case STREAM_IS_DOLBY_AUDIO :
        case STREAM_IS_DTS_AUDIO :
            stream_id_type = PES_STEAM_ID_EXTENDED_STREAM_ID;
            break;
        case STREAM_IS_EXTENDED_VIDEO :
            stream_id_type = PES_STEAM_ID_EXTENDED_STREAM_ID;
            break;
        case STREAM_IS_ARIB_CAPTION :
            stream_id_type = PES_STEAM_ID_PRIVATE_STREAM_1;
            break;
        case STREAM_IS_ARIB_STRING_SUPER :
            stream_id_type = PES_STEAM_ID_PRIVATE_STREAM_2;
            break;
        case STREAM_IS_DSMCC :
            stream_id_type = PES_STEAM_ID_DSMCC_STREAM;
            break;
        default :
            if( stream_judge & STREAM_IS_VIDEO )
                stream_id_type = PES_STEAM_ID_VIDEO_STREAM;
            else if( stream_judge & STREAM_IS_AUDIO )
                stream_id_type = PES_STEAM_ID_AUDIO_STREAM;
            break;
    }
    return stream_id_type;
}

#define READ_DESCRIPTOR( name )         \
static void read_##name##_descriptor( uint8_t *descriptor, name##_descriptor_info_t *name )

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
    registration->format_identifier              = (descriptor[2] << 24)
                                                 | (descriptor[3] << 16)
                                                 | (descriptor[4] <<  8)
                                                 |  descriptor[5];
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
    target_background_grid->vertical_size            = ((descriptor[3] & 0x02) << 12)
                                                     |  (descriptor[4]         <<  4)
                                                     |  (descriptor[5]         >>  4);
    target_background_grid->aspect_ratio_information =   descriptor[5] & 0x04;
}

READ_DESCRIPTOR( video_window )
{
#if ENABLE_SUPPRESS_WARNINGS
    (void) descriptor;
#endif
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
        ISO_639_language->data[i].ISO_639_language_code = (descriptor[index+0] << 16)
                                                        | (descriptor[index+1] <<  8)
                                                        |  descriptor[index+2];
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
    private_data_indicator->private_data_indicator = (descriptor[2] << 24)
                                                   | (descriptor[3] << 16)
                                                   | (descriptor[4] <<  8)
                                                   |  descriptor[5];
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
extern void mpeg_stream_get_descriptor_info
(
 /* mpeg_stream_type            stream_type, */
    uint8_t                    *descriptor,
    mpeg_descriptor_info_t     *descriptor_info
)
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
        mapi_log( LOG_LV2,                          \
                  "[check] "#name"_descriptor\n"    \
                  __VA_ARGS__ );                    \
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
                mapi_log( LOG_LV2, "        additional_info:" );
                for( uint8_t i = 6; i < descriptor_info->length; ++i )
                    mapi_log( LOG_LV2, " 0x%02X", descriptor_info->registration.additional_identification_info[i] );
                mapi_log( LOG_LV2, "\n" );
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
                "        CA_system_ID:0x%04X\n"
                "        CA_PID:0x%04X\n"
                , descriptor_info->conditional_access.CA_system_ID
                , descriptor_info->conditional_access.CA_PID
            )
            break;
        PRINT_DESCRIPTOR_INFO( ISO_639_language )
            for( uint8_t i = 0; i < descriptor_info->ISO_639_language.data_num; ++i )
                mapi_log( LOG_LV2,
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
                mapi_log( LOG_LV2,
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

extern mpeg_stream_group_type mpeg_stream_judge_type
(
    mpeg_stream_type            stream_type,
    uint16_t                    descriptor_num,
    uint8_t                    *descriptor_data
)
{
    mpeg_stream_group_type stream_judge = STREAM_IS_UNKNOWN;
    uint16_t idx = 0;
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
            stream_judge = STREAM_IS_DSMCC;
            break;
        case STREAM_VIDEO_MP4 :
            stream_judge = STREAM_IS_MPEG4_VIDEO;
            break;
        case STREAM_VIDEO_AVC :
            stream_judge = STREAM_IS_VIDEO;
            break;
        case STREAM_VIDEO_HEVC :
            stream_judge = STREAM_IS_VIDEO;
            break;
        case STREAM_VIDEO_VC1 :
            stream_judge = STREAM_IS_EXTENDED_VIDEO;
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
                if( descriptor_data[idx + 0] == video_stream_descriptor )
                    stream_judge = STREAM_IS_PRIVATE_VIDEO;
                else if( descriptor_data[idx + 0] == registration_descriptor )
                    stream_judge = STREAM_IS_PCM_AUDIO;
                else
                {
                    idx += 2 + descriptor_data[idx + 1];
                    continue;
                }
                break;
            }
            break;
      //case STREAM_AUDIO_AC3_DTS :
        case STREAM_AUDIO_AC3 :
        case STREAM_AUDIO_DDPLUS :
        case STREAM_AUDIO_DDPLUS_SUB :
            stream_judge = STREAM_IS_DOLBY_AUDIO;
            break;
        case STREAM_AUDIO_DTS :
        case STREAM_AUDIO_DTS_HD :
        case STREAM_AUDIO_DTS_HD_XLL :
        case STREAM_AUDIO_DTS_HD_SUB :
            stream_judge = STREAM_IS_DTS_AUDIO;
            break;
        case STREAM_PES_PRIVATE_DATA :
            for( uint16_t i = 0; i < descriptor_num; ++i )
            {
                if( descriptor_data[idx + 0] == 0x52 && descriptor_data[idx + 1] == 1 )
                {
                    if( descriptor_data[idx + 2] == 0x30 )
                        stream_judge = STREAM_IS_ARIB_CAPTION;
                    else if( descriptor_data[idx + 2] == 0x38 )
                        stream_judge = STREAM_IS_ARIB_STRING_SUPER;
                }
                if( stream_judge != STREAM_IS_UNKNOWN )
                    break;
                idx += 2 + descriptor_data[idx + 1];
            }
            break;
        default :
            break;
    }
    return stream_judge;
}

typedef int (*header_check_func)( uint8_t *header, mpeg_stream_raw_info_t *stream_raw_info );

static int mpa_header_check( uint8_t *header, mpeg_stream_raw_info_t *stream_raw_info )
{
    if( header[0] != 0xFF || (header[1] & 0xE0) != 0xE0 )
        return -1;
    int version_id               = (header[1] & 0x18) >> 3;
    if( version_id == 0x01 )                    /* MPEG Audio version ID : '01' reserved            */
        return -1;
    int layer                    = (header[1] & 0x06) >> 1;
    if( !layer )                                /* layer : '00' reserved                            */
        return -1;
    /* protection_bit            =  header[1] & 0x01);              */
    int bitrate_index            =  header[2] >> 4;
    if( bitrate_index == 0x0F )                 /* bitrate index : '1111' bad                       */
        return -1;
    int sampling_frequency_index = (header[2] & 0x0C) >> 2;
    if( sampling_frequency_index == 0x03 )      /* Sampling rate frequency index : '11' reserved    */
        return -1;
    uint8_t channel_mode         = (header[3] & 0x60) >> 5;
    /* check bitrate & channel_mode matrix. */
#if 0
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
#endif
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
    int      vl_index = version_layer_matrix[version_id - 1][layer - 1];
    uint16_t bitrate  = bitrate_matrix[vl_index][bitrate_index];
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
    mapi_log( LOG_LV4, "[debug] [MPEG-Audio] detect header.\n" );
    /* setup. */
    if( stream_raw_info )
    {
        static const uint32_t sampling_frequency_base[3] = { 44100, 48000, 32000 };
        static const uint16_t speaker_mapping[4] =
            {
                MPEG_AUDIO_SPEAKER_F_2CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS | MPEG_AUDIO_SPEAKER_JOINT_STEREO,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS | MPEG_AUDIO_SPEAKER_DUAL_MONO   ,
                MPEG_AUDIO_SPEAKER_FRONT_CENTER
            };
        static const uint8_t mpa_layer[4] = { 0, 3, 2, 1 };
        stream_raw_info->sampling_frequency = sampling_frequency_base[sampling_frequency_index]
                                            / ((version_id == 0) ? 4 : (version_id == 2) ? 2 : 1 );
        stream_raw_info->bitrate            = bitrate * 1000;
        stream_raw_info->channel            = speaker_mapping[channel_mode];
        stream_raw_info->layer              = mpa_layer[layer];
    }
    return 0;
}

static int aac_header_check( uint8_t *header, mpeg_stream_raw_info_t *stream_raw_info )
{
    if( header[0] != 0xFF || (header[1] & 0xF0) != 0xF0 )
        return -1;
    if( header[1] & 0x06 )                      /* layer : allways 0                                        */
        return -1;
    if( (header[2] & 0xC0) == 0xC0 )            /* profile : '11' reserved                                  */
        return -1;
    int sampling_frequency_index   = (header[2] & 0x3C) >> 2;
    if( sampling_frequency_index > 11 )         /* sampling_frequency_index : 0xc-0xf reserved              */
        return -1;
    uint32_t frame_length          = ((header[3] & 0x03) << 11) | (header[4] << 3) | (header[5] >> 5);
    if( !frame_length )                         /* aac frame length                             */
        return -1;
    /* protection absent           =   header[1] & 0x01;            */
    uint8_t  channel_configuration = ((header[2] & 0x01) << 2) | (header[3] >> 6);
    /* detect header. */
    mapi_log( LOG_LV4, "[debug] [ADTS-AAC] detect header. frame_len:%d\n", frame_length );
    /* setup. */
    if( stream_raw_info )
    {
        static const uint32_t sampling_frequency_base[3] = { 96000, 88200, 64000 };
        static const uint16_t speaker_mapping[8] =
            {
                MPEG_AUDIO_SPEAKER_F_2CHANNELS | MPEG_AUDIO_SPEAKER_DUAL_MONO,
                MPEG_AUDIO_SPEAKER_FRONT_CENTER,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS | MPEG_AUDIO_SPEAKER_REAR_SRROUND,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS | MPEG_AUDIO_SPEAKER_LFE_CHANNEL ,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS | MPEG_AUDIO_SPEAKER_O_2CHANNELS | MPEG_AUDIO_SPEAKER_LFE_CHANNEL
            };
        stream_raw_info->frame_length       = frame_length;
        stream_raw_info->sampling_frequency = sampling_frequency_base[sampling_frequency_index % 3] / (1 << (int)(sampling_frequency_index / 3));
        stream_raw_info->bitrate            = 0;        // FIXME
        stream_raw_info->channel            = speaker_mapping[channel_configuration];
    }
    return 0;
}

static int lpcm_header_check( uint8_t *header, mpeg_stream_raw_info_t *stream_raw_info )
{
    /* MPEG-2 Program Stream   : 2byte  (no support)    */          // FIXME
    /* MPEG-2 Transport Stream : 4byte                  */          // FIXME
    uint32_t frame_length       =   (header[0] << 8) | header[1];
    uint8_t  channel_assignment =   (header[2] & 0xF0) >> 4;
    uint8_t  sampling_frequency =    header[2] & 0x0F;
    uint8_t  bits_per_sample    =   (header[3] & 0xC0) >> 6;
    /* start_flag               = !!(header[3] & 0x20);             */
    /* reserved     5bit                                            */
    /* check. */
    if( !frame_length )
        return -1;
    if( channel_assignment == 0 || channel_assignment == 2 || channel_assignment > 11 )
        return -1;
    if( sampling_frequency == 0 || sampling_frequency == 2 || sampling_frequency == 3 || sampling_frequency > 5 )
        return -1;
    if( bits_per_sample == 0 || bits_per_sample > 3 )
        return -1;
    /* detect header. */
    mapi_log( LOG_LV4, "[debug] [LPCM] detect header.\n" );
    /* setup. */
    if( stream_raw_info )
    {
        static const uint16_t speaker_mapping[12] =
            {
                0,
                MPEG_AUDIO_SPEAKER_FRONT_CENTER,
                0,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS | MPEG_AUDIO_SPEAKER_REAR_SRROUND,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS | MPEG_AUDIO_SPEAKER_REAR_SRROUND,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS | MPEG_AUDIO_SPEAKER_LFE_CHANNEL ,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS | MPEG_AUDIO_SPEAKER_O_2CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS | MPEG_AUDIO_SPEAKER_O_2CHANNELS | MPEG_AUDIO_SPEAKER_LFE_CHANNEL
            };
        stream_raw_info->frame_length       = frame_length;
        stream_raw_info->sampling_frequency = (sampling_frequency == 1) ?  48000
                                            : (sampling_frequency == 4) ?  96000
                                            : (sampling_frequency == 5) ? 192000
                                            :                                  0;
        stream_raw_info->channel            = speaker_mapping[channel_assignment];
        stream_raw_info->bit_depth          = 12 + 4 * bits_per_sample;
    }
    return 0;
}

static const uint16_t ac3_speaker_mapping[8] =
    {
        MPEG_AUDIO_SPEAKER_F_2CHANNELS | MPEG_AUDIO_SPEAKER_DUAL_MONO   ,
        MPEG_AUDIO_SPEAKER_FRONT_CENTER,
        MPEG_AUDIO_SPEAKER_F_2CHANNELS ,
        MPEG_AUDIO_SPEAKER_F_3CHANNELS ,
        MPEG_AUDIO_SPEAKER_F_2CHANNELS | MPEG_AUDIO_SPEAKER_REAR_SRROUND,
        MPEG_AUDIO_SPEAKER_F_3CHANNELS | MPEG_AUDIO_SPEAKER_REAR_SRROUND,
        MPEG_AUDIO_SPEAKER_F_2CHANNELS | MPEG_AUDIO_SPEAKER_O_2CHANNELS ,
        MPEG_AUDIO_SPEAKER_F_3CHANNELS | MPEG_AUDIO_SPEAKER_O_2CHANNELS
    };

static int ac3_header_check( uint8_t *header, mpeg_stream_raw_info_t *stream_raw_info )
{
    /* check Synchronization Information. */
    if( header[0] != 0x0B || header[1] != 0x77 )
        return -1;
    /* crc1  16bit               =  (header[2] << 8) | header[3];   */
    uint8_t  sample_rate_code    =   header[4] >> 6;
    if( sample_rate_code == 0x03 )
        return -1;
    uint8_t  frame_size_code     =   header[4] & 0x3F;
    if( frame_size_code > 37 )
        return -1;
    /* check Bit Stream Information. */
    /* bit_stream_identification =   header[5] >> 3;                */
    /* bit_stream_mode           =   header[5] & 0x07;              */
    uint8_t  audio_coding_mode   =   header[6] >> 5;
    uint16_t bsi_data            = ((header[6] & 0x1F) << 8) | header[7];
    uint8_t  lfe_bit_offset      = 2 * ( ((audio_coding_mode & 0x01) && audio_coding_mode != 0x01)
                                       + (audio_coding_mode & 0x04)
                                       + (audio_coding_mode == 0x02) );
    uint8_t  lfe_channel_on      = !!(bsi_data & (0x0100 >> lfe_bit_offset));
    /* detect header. */
    mapi_log( LOG_LV4, "[debug] [AC-3] detect header.\n" );
    /* setup. */
    if( stream_raw_info )
    {
        static const uint16_t ac3_sample_rate[3] = { 48000, 44100, 32000 };
        static const uint16_t ac3_bitrate[19] =
            {
                 32,  40,  48,  56,  64,  80,  96, 112, 128,
                160, 192, 224, 256, 320, 384, 448, 512, 576, 640
            };
        stream_raw_info->frame_length       = ac3_bitrate[frame_size_code / 2] * 192000 / ac3_sample_rate[sample_rate_code]
                                            + (sample_rate_code == 0x01 && (frame_size_code & 0x01));
        stream_raw_info->sampling_frequency = ac3_sample_rate[sample_rate_code];
        stream_raw_info->bitrate            = ac3_bitrate[frame_size_code / 2] * 1000;
        stream_raw_info->channel            = ac3_speaker_mapping[audio_coding_mode]
                                            | (lfe_channel_on ? MPEG_AUDIO_SPEAKER_LFE_CHANNEL : 0);
    }
    return 0;
}

static int eac3_header_check( uint8_t *header, mpeg_stream_raw_info_t *stream_raw_info )
{
    /* check Synchronization Information. */
    if( header[0] != 0x0B || header[1] != 0x77 )
        return -1;
    /* check Bit Stream Information. */
    uint8_t stream_type             =    header[2] >> 6;
    /* substream_identification     =   (header[2] & 0x38) >> 6;    */
    uint8_t frame_size              =  ((header[2] & 0x07) << 8) | header[3];
    uint8_t sample_rate_code        =    header[4] >> 6;
    uint8_t sample_rate_code2       =    0;
    uint8_t number_of_audio_blocks  =   (header[4] & 0x30) >> 4;
    if( sample_rate_code == 0x03 )
    {
        sample_rate_code2           =    number_of_audio_blocks;
        number_of_audio_blocks      =    0x03;
    }
    uint8_t audio_coding_mode       =   (header[4] & 0x0E) >> 1;
    uint8_t lfe_channel_on          =    header[4] & 0x01;
    /* bit_stream_identification    =    header[5] >> 3;            */
    /* dialogue_normalization       =  ((header[5] & 0x07) << 2) | (header[6] >> 6);    */
    int header_idx = 6;
    /* compression_gain_word_exists = !!(header[6] & 0x20)          */
    if( header[6] & 0x20 )
    {
        /* compression_gain_word    =  ((header[6] & 0x1F) << 3) | (header[7] >> 5);    */
        ++header_idx;
    }
    uint8_t  custom_channel_map_exists = 0;
    uint16_t custom_channel_map        = 0;
    if( stream_type == 1 )
    {
        if( audio_coding_mode == 0 )
        {
            /* dialogue_normalization2       =  ((header[header_idx+0] & 0x01) << 4)
                                             |   (header[header_idx+1]         >> 4);   */
            /* compression_gain_word2_exists = !!(header[header_idx+1] & 0x08)          */
            if( header[header_idx+1] & 0x08 )
            {
                /* compression_gain_word2    =  ((header[header_idx+1] & 0x07) << 5)
                                             |   (header[header_idx+2]         >> 3);   */
                ++header_idx;
            }
            custom_channel_map_exists        = !!(header[header_idx+1] & 0x04);
            if( custom_channel_map_exists )
                custom_channel_map           =  ((header[header_idx+1] & 0x03) << 14)
                                             |   (header[header_idx+2]         <<  6)
                                             |   (header[header_idx+2]         >>  2);
        }
        else
        {
            custom_channel_map_exists        =    header[header_idx+0] & 0x01;
            if( custom_channel_map_exists )
                custom_channel_map           =   (header[header_idx+1] << 8) | header[header_idx+2];
        }
    }
    /* detect header. */
    mapi_log( LOG_LV4, "[debug] [EAC-3] detect header.\n" );
    /* setup. */
    if( stream_raw_info )
    {
        static const uint16_t eac3_sample_rate[7] = { 48000, 44100, 32000, 24000, 22050, 16000, 0 };
        stream_raw_info->frame_length       = frame_size + 1;
        stream_raw_info->sampling_frequency = eac3_sample_rate[sample_rate_code + sample_rate_code2];
        stream_raw_info->bitrate            = 0;        // FIXME
        stream_raw_info->channel            = ac3_speaker_mapping[audio_coding_mode]
                                            | (lfe_channel_on ? MPEG_AUDIO_SPEAKER_LFE_CHANNEL : 0);
        if( custom_channel_map_exists )
            stream_raw_info->channel       |= ((custom_channel_map & 0x8000) ? MPEG_AUDIO_SPEAKER_FRONT_LEFT   : 0)
                                            | ((custom_channel_map & 0x4000) ? MPEG_AUDIO_SPEAKER_FRONT_CENTER : 0)
                                            | ((custom_channel_map & 0x2000) ? MPEG_AUDIO_SPEAKER_FRONT_RIGHT  : 0)
                                            | ((custom_channel_map & 0x1000) ? MPEG_AUDIO_SPEAKER_REAR_LEFT    : 0)
                                            | ((custom_channel_map & 0x0800) ? MPEG_AUDIO_SPEAKER_REAR_RIGHT   : 0)
                                            | ((custom_channel_map & 0x0001) ? MPEG_AUDIO_SPEAKER_LFE_CHANNEL  : 0);
    }
    return 0;
}

static int dts_header_check( uint8_t *header, mpeg_stream_raw_info_t *stream_raw_info )
{
    if( header[0] != 0x7F || header[1] != 0xFE || header[2] != 0x80 || header[3] != 0x01 )
        return -1;
    /* frame_type                           = !!(header[4] & 0x80);         */
    /* deficit_sample_count                 =   (header[4] & 0x7C) >> 2;    */
    uint8_t  crc_present_flag               = !!(header[4] & 0x02);
    /* number_of_pcm_sample_blocks          =  ((header[4] & 0x01) <<  6) | (header[5] >> 2);   */
    uint16_t primary_frame_byte_size        =  ((header[5] & 0x03) << 12) | (header[6] << 4) | (header[7] >> 4);
    if( primary_frame_byte_size < 95 )
        return -1;
    uint8_t  audio_channel_arangement       =  ((header[7] & 0x0F) <<  2) | (header[8] >> 6);
    uint8_t  core_audio_sampling_frequency  =   (header[8] & 0x3C) >> 2;
    if( (core_audio_sampling_frequency % 5) == 0 || (core_audio_sampling_frequency % 5) == 4 )
        return -1;
    uint8_t  transmission_bit_rate          =  ((header[8] & 0x03) <<  3) | (header[9] >> 5);
    if( transmission_bit_rate == 29 )       /* 0b11101      open            */
        /* correct table index. */
        transmission_bit_rate = 25;
    else if( transmission_bit_rate > 24 )   /* other codes  invalid         */
        return -1;
    /* fixed_bit  always '0'                = !!(header[9] & 0x10);         */
    if( header[9] & 0x10 )
        return -1;
    /* embedded_dynamic_range_flag          = !!(header[9] & 0x08);         */
    /* embedded_time_stamp_flag             = !!(header[9] & 0x04);         */
    /* auxiliary_data_flag                  = !!(header[9] & 0x02);         */
    /* hdcd                                 =    header[9] & 0x01;          */
    /* extension_audio_descriptor_flag      =    header[10] >> 5;           */
    /* extended_coding_flag                 = !!(header[10] & 0x10);        */
    /* audio_sync_word_insertion_flag       = !!(header[10] & 0x08);        */
    uint8_t  low_frequency_effects_flag     =   (header[10] & 0x06) >> 1;
    /* predictor_history_flag_switch        =    header[10] & 0x01;         */
    int header_idx = 11;
    if( crc_present_flag )
    {
        /* Header CRC Check Bytes           =   (header[header_idx+0] << 8) | header[header_idx+1]; */
        header_idx += 2;
    }
    /* multirate_interpolator_switch        = !!(header[header_idx] & 0x80);        */
    /* encoder_software_revision            =   (header[header_idx] & 0x78) >> 3;   */
    /* copy_history                         =   (header[header_idx] & 0x06) >> 1;   */
    uint8_t  source_pcm_resolution          =  ((header[header_idx+0] & 0x01) << 2) | (header[header_idx+1] >> 6);
    if( source_pcm_resolution == 4 || source_pcm_resolution == 7 )
        return -1;
    /* detect header. */
    mapi_log( LOG_LV4, "[debug] [DTS] detect header.\n" );
    /* setup. */
    if( stream_raw_info )
    {
        static const uint16_t dts_sample_rate_base[3] = {  8000, 11025, 12000 };
        static const uint32_t dts_bitrate[26] =
            {
                  32000,   56000,   64000,   96000,  112000,  128000,  192000,  224000,  256000,
                 320000,  384000,  448000,  512000,  576000,  640000,  768000,  960000, 1024000,
                1152000, 1280000, 1344000, 1408000, 1411200, 1472000, 1536000,       0
            };
        static const uint16_t speaker_mapping[17] =
            {
                MPEG_AUDIO_SPEAKER_FRONT_CENTER ,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS  | MPEG_AUDIO_SPEAKER_DUAL_MONO   ,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS  ,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS  ,   /* (L+R) + (L-R) (sum-difference)   */
                MPEG_AUDIO_SPEAKER_F_2CHANNELS  ,   /* LT +RT (left and right total)    */
                MPEG_AUDIO_SPEAKER_F_3CHANNELS  ,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS  | MPEG_AUDIO_SPEAKER_REAR_SRROUND,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS  | MPEG_AUDIO_SPEAKER_REAR_SRROUND,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS  | MPEG_AUDIO_SPEAKER_R_2CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS  | MPEG_AUDIO_SPEAKER_R_2CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS  | MPEG_AUDIO_SPEAKER_O_2CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS ,
                MPEG_AUDIO_SPEAKER_FRONT_CENTER | MPEG_AUDIO_SPEAKER_F_2CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS | MPEG_AUDIO_SPEAKER_OVERHEAD    ,
                MPEG_AUDIO_SPEAKER_FRONT_CENTER | MPEG_AUDIO_SPEAKER_REAR_CENTER | MPEG_AUDIO_SPEAKER_F_2CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS  | MPEG_AUDIO_SPEAKER_O_2CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS ,
                MPEG_AUDIO_SPEAKER_F_2CHANNELS  | MPEG_AUDIO_SPEAKER_O_2CHANNELS | MPEG_AUDIO_SPEAKER_R_2CHANNELS | MPEG_AUDIO_SPEAKER_R2_2CHANNELS,
                MPEG_AUDIO_SPEAKER_F_3CHANNELS  | MPEG_AUDIO_SPEAKER_O_2CHANNELS | MPEG_AUDIO_SPEAKER_R_3CHANNELS ,
                0
            };
        stream_raw_info->frame_length       = primary_frame_byte_size + 1;
        stream_raw_info->sampling_frequency = dts_sample_rate_base[core_audio_sampling_frequency / 5] * (1 << ((core_audio_sampling_frequency % 5) - 1));
        stream_raw_info->bitrate            = dts_bitrate[transmission_bit_rate];
        stream_raw_info->channel            = speaker_mapping[audio_channel_arangement]
                                            | ((low_frequency_effects_flag == 1 || low_frequency_effects_flag == 2) ? MPEG_AUDIO_SPEAKER_LFE_CHANNEL : 0);
        stream_raw_info->bit_depth          = (source_pcm_resolution & 4) ? 24 : (source_pcm_resolution & 2) ? 20 : 16;
    }
    return 0;
}

extern int32_t mpeg_stream_check_header
(
    mpeg_stream_type            stream_type,
    mpeg_stream_group_type      stream_judge,
    int                         search_point,
    uint8_t                    *buffer,
    uint32_t                    buffer_size,
    mpeg_stream_raw_info_t     *stream_raw_info,
    int32_t                    *data_offset
)
{
    int32_t header_offset = -1;
    if( stream_raw_info )
        memset( stream_raw_info, 0, sizeof(mpeg_stream_raw_info_t) );
    if( data_offset )
        *data_offset = 0;
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
                int index   = (stream_judge == STREAM_IS_AAC_AUDIO);
                int idx_max = buffer_size - header_check[index].size;
                for( int i = 0; i <= idx_max; ++i )
                {
                    if( header_check[index].func( &(buffer[i]), stream_raw_info ) )
                        continue;
                    /* setup. */
                    header_offset = i;
                    break;
                }
            }
            break;
        case STREAM_IS_PCM_AUDIO :
            if( buffer_size >= STREAM_LPCM_HEADER_CHECK_SIZE )
            {
                if( !lpcm_header_check( buffer, stream_raw_info ) )
                {
                    /* setup. */
                    /* 0 (start point) :  skip header size.             */
                    /* 1 ( end  point) :  return end point.             */
                    header_offset = search_point ? 0 : STREAM_LPCM_HEADER_CHECK_SIZE;
                    if( data_offset )
                        *data_offset = search_point ? STREAM_LPCM_HEADER_CHECK_SIZE : 0;
                }
            }
            break;
        case STREAM_IS_DOLBY_AUDIO :
            {
                static const struct {
                    header_check_func   func;
                    int                 size;
                } header_check[2] =
                    {
                        { ac3_header_check , STREAM_AC3_HEADER_CHECK_SIZE  },
                        { eac3_header_check, STREAM_EAC3_HEADER_CHECK_SIZE }
                    };
                int index = !(stream_type == STREAM_AUDIO_AC3);
                if( (int)buffer_size >= header_check[index].size )
                {
                    if( !header_check[index].func( buffer, stream_raw_info ) )
                        /* setup. */
                        header_offset = 0;
                }
            }
            break;
        case STREAM_IS_DTS_AUDIO :
            if( buffer_size >= STREAM_DTS_HEADER_CHECK_SIZE )
            {
                if( !dts_header_check( buffer, stream_raw_info ) )
                    /* setup. */
                    header_offset = 0;
            }
            break;
        default :
            header_offset = 0;
            break;
    }
    if( header_offset >= 0 && (stream_judge & STREAM_IS_AUDIO) )
        mapi_log( LOG_LV4, "        check_buffer_size:%d  header_offset:%d\n", buffer_size, header_offset );
    //if( stream_raw_info )
    //    mapi_log( LOG_LV4, "[debug] %6uHz  %7uKbps  %u channel  other:%u\n"
    //                     , stream_raw_info->sampling_frequency, stream_raw_info->bitrate, stream_raw_info->channel
    //                     , stream_raw_info->layer );
    return header_offset;
}

extern int mpeg_stream_check_header_skip( mpeg_stream_group_type stream_judge )
{
    return stream_judge == STREAM_IS_PCM_AUDIO;
}

extern uint32_t mpeg_stream_get_header_check_size( mpeg_stream_type stream_type, mpeg_stream_group_type stream_judge )
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
            check_size = (stream_type == STREAM_AUDIO_AC3) ? STREAM_AC3_HEADER_CHECK_SIZE : STREAM_EAC3_HEADER_CHECK_SIZE;
            break;
        case STREAM_IS_DTS_AUDIO :
            check_size = STREAM_DTS_HEADER_CHECK_SIZE;
            break;
        default :
            break;
    }
    return check_size;
}
