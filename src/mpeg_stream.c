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
    return ((int64_t)(time_stamp_data[0] & 0x0E) << 29)
         | (          time_stamp_data[1]         << 22)
         | (         (time_stamp_data[2] & 0xFE) << 14)
         | (          time_stamp_data[3]         <<  7)
         | (         (time_stamp_data[4] & 0xFE) >>  1);
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
    registration->format_identifier = (descriptor[2] << 24)
                                    | (descriptor[3] << 16)
                                    | (descriptor[4] <<  8)
                                    |  descriptor[5];
    /* additional_identification_info */
    registration->additional_identification_info_length = descriptor[1] - 4;
    if( registration->additional_identification_info_length )
        memcpy( registration->additional_identification_info, &(descriptor[6]), registration->additional_identification_info_length );

}

READ_DESCRIPTOR( data_stream_alignment )
{
    data_stream_alignment->alignment_type = descriptor[2];
}

READ_DESCRIPTOR( target_background_grid )
{
    target_background_grid->horizontal_size          =  (descriptor[2] << 6) | (descriptor[3] >> 2);
    target_background_grid->vertical_size            = ((descriptor[3] & 0x02) << 12)
                                                     | ( descriptor[4]         <<  4)
                                                     | ( descriptor[5]         >>  4);
    target_background_grid->aspect_ratio_information =   descriptor[5] & 0x04;
}

READ_DESCRIPTOR( video_window )
{
    video_window->horizontal_offset =   descriptor[2] << 6 | (descriptor[3] & 0xFC) >> 2;
    video_window->vertical_offset   = ((descriptor[3] & 0x03) << 12)
                                    | ( descriptor[4]         <<  4)
                                    | ((descriptor[5] & 0xF0) >>  4);
    video_window->window_priority   =   descriptor[5] & 0x0F;
}

READ_DESCRIPTOR( conditional_access )
{
    conditional_access->CA_system_ID =  (descriptor[2] << 8) | descriptor[3];
    /* reserved     3bit             =  (descriptor[4] & 0x30) >> 5; */
    conditional_access->CA_PID       = ((descriptor[4] & 0x1F) << 8) | descriptor[5];
    /* private_data_byte */
    conditional_access->private_data_byte_length = descriptor[1] - 4;
    if( conditional_access->private_data_byte_length )
        memcpy( conditional_access->private_data_byte, &(descriptor[6]), conditional_access->private_data_byte_length );
}

READ_DESCRIPTOR( ISO_639_language )
{
    ISO_639_language->data_num                          =  descriptor[1] >> 2;
    uint8_t idx = 2;
    for( uint8_t i = 0; i < ISO_639_language->data_num; ++i )
    {
        ISO_639_language->data[i].ISO_639_language_code = (descriptor[idx+0] << 16)
                                                        | (descriptor[idx+1] <<  8)
                                                        |  descriptor[idx+2];
        ISO_639_language->data[i].audio_type            =  descriptor[idx+3];
        idx += 4;
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
    copyright->copyright_identifier = (descriptor[2] << 24)
                                    | (descriptor[3] << 16)
                                    | (descriptor[4] <<  8)
                                    |  descriptor[5];
    /* additional_copyright_info */
    copyright->additional_copyright_info_length = descriptor[1] - 4;
    if( copyright->additional_copyright_info_length )
        memcpy( copyright->additional_copyright_info, &(descriptor[6]), copyright->additional_copyright_info_length );
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
 // InitialObjectDescriptor();  /* defined in 8.6.3.1 of ISO/IEC 14496-1. */    // FIXME
    IOD->InitialObjectDescriptor_length = descriptor[1] - 2;
    if( IOD->InitialObjectDescriptor_length )
        memcpy( IOD->InitialObjectDescriptor_data, &(descriptor[4]), IOD->InitialObjectDescriptor_length );
}

READ_DESCRIPTOR( SL )
{
    SL->ES_ID = (descriptor[2] << 8) | descriptor[3];
}

READ_DESCRIPTOR( FMC )
{
    FMC->data_num = descriptor[1] / 3;
    uint8_t idx = 2;
    for( uint8_t i = 0; i < FMC->data_num; ++i )
    {
        FMC->data[i].ES_ID          = (descriptor[idx+0] << 8) | descriptor[idx+1];
        FMC->data[i].FlexMuxChannel =  descriptor[idx+2];
        idx += 3;
    }
}

READ_DESCRIPTOR( External_ES_ID )
{
    External_ES_ID->External_ES_ID = (descriptor[2] << 8) | descriptor[3];
}

READ_DESCRIPTOR( MuxCode )
{
 // MuxCodeTableEntry();        /* defined in 11.2.4.3 of ISO/IEC 14496-1. */
    MuxCode->length            =  descriptor[2];
    MuxCode->MuxCode           = (descriptor[3] & 0xF0) >> 4;
    MuxCode->version           =  descriptor[3] & 0x0F;
    MuxCode->substructureCount =  descriptor[4];
    uint8_t idx = 5;
    for( uint8_t i = 0; i < MuxCode->substructureCount; ++i )
    {
        MuxCode->subs[i].slotCount       = (descriptor[idx] & 0xF8) >> 3;
        MuxCode->subs[i].repetitionCount =  descriptor[idx] & 0x07;
        ++idx;
        for( uint8_t k = 0; k < MuxCode->subs[i].slotCount; ++k )
        {
            MuxCode->subs[i].slots[k].flexMuxChannel = descriptor[idx+0];
            MuxCode->subs[i].slots[k].numberOfBytes  = descriptor[idx+1];
            idx += 2;
        }
    }
}

READ_DESCRIPTOR( FmxBufferSize )
{
 // DefaultFlexMuxBufferDescriptor()    /* defined in 11.2 of ISO/IEC 14496-1. */   // FIXME
 // FlexMuxBufferDescriptor()           /* defined in 11.2 of ISO/IEC 14496-1. */   // FIXME
    FmxBufferSize->descriptor_length = descriptor[1];
    if( FmxBufferSize->descriptor_length )
        memcpy( FmxBufferSize->descriptor_data, &(descriptor[2]), FmxBufferSize->descriptor_length );
}

READ_DESCRIPTOR( MultiplexBuffer )
{
    MultiplexBuffer->MB_buffer_size = (descriptor[2] << 16) | (descriptor[3] << 8) | descriptor[4];
    MultiplexBuffer->TB_leak_rate   = (descriptor[5] << 16) | (descriptor[6] << 8) | descriptor[7];
}

READ_DESCRIPTOR( content_labeling )
{
    content_labeling->metadata_application_format = (descriptor[2] << 8) | descriptor[3];
    uint8_t idx;
    if( content_labeling->metadata_application_format == 0xFFFF )
    {
        content_labeling->metadata_application_format_identifier = (descriptor[4] << 24)
                                                                 | (descriptor[5] << 16)
                                                                 | (descriptor[6] <<  8)
                                                                 |  descriptor[7];
        idx = 8;
    }
    else
        idx = 4;
    content_labeling->content_reference_id_record_flag = !!(descriptor[idx] & 0x80);
    content_labeling->content_time_base_indicator      =   (descriptor[idx] & 0x78) >> 3;
    /* reserved                                        =    descriptor[idx] & 0x07; */
    ++ idx;
    if( content_labeling->content_reference_id_record_flag )
    {
        content_labeling->content_reference_id_record_length = descriptor[idx];
        ++idx;
        if( content_labeling->content_reference_id_record_length )
        {
            memcpy( content_labeling->content_reference_id_byte, &(descriptor[idx]), content_labeling->content_reference_id_record_length );
            idx += content_labeling->content_reference_id_record_length;
        }
    }
    if( content_labeling->content_time_base_indicator == 1 || content_labeling->content_time_base_indicator == 2 )
    {
        /* reserved                                =            (descriptor[idx+0] & 0xE0) >> 1; */
        content_labeling->content_time_base_value  = ((uint64_t)(descriptor[idx+0] & 0x01) << 32)
                                                   | (           descriptor[idx+1]         << 24)
                                                   | (           descriptor[idx+2]         << 16)
                                                   | (           descriptor[idx+3]         <<  8)
                                                   |             descriptor[idx+4];
        idx += 5;
        /* reserved                                =            (descriptor[idx+0] & 0xE0) >> 1; */
        content_labeling->metadata_time_base_value = ((uint64_t)(descriptor[idx+0] & 0x01) << 32)
                                                   | (           descriptor[idx+1]         << 24)
                                                   | (           descriptor[idx+2]         << 16)
                                                   | (           descriptor[idx+3]         <<  8)
                                                   |             descriptor[idx+4];
        idx += 5;
    }
    if( content_labeling->content_time_base_indicator == 2 )
    {
        /* reserved                 = !!(descriptor[idx] & 0x80); */
        content_labeling->contentId =    descriptor[idx] & 0x7F;
        ++idx;
    }
    if( 3 <= content_labeling->content_time_base_indicator && content_labeling->content_time_base_indicator <= 7 )
    {
        content_labeling->time_base_association_data_length = descriptor[idx];
        ++idx;
        if( content_labeling->time_base_association_data_length )
        {
            memcpy( content_labeling->time_base_association_data, &(descriptor[idx]), content_labeling->time_base_association_data_length );
            idx += content_labeling->time_base_association_data_length;
        }
    }
    /* private_data_byte */
    content_labeling->private_data_byte_length = descriptor[1] - idx + 2;
    if( content_labeling->private_data_byte_length )
        memcpy( content_labeling->private_data_byte, &(descriptor[idx]), content_labeling->private_data_byte_length );
}

READ_DESCRIPTOR( metadata_pointer )
{
    metadata_pointer->metadata_application_format = (descriptor[2] << 8) | descriptor[3];
    uint8_t idx;
    if( metadata_pointer->metadata_application_format == 0xFFFF )
    {
        metadata_pointer->metadata_application_format_identifier = (descriptor[4] << 24)
                                                                 | (descriptor[5] << 16)
                                                                 | (descriptor[6] <<  8)
                                                                 |  descriptor[7];
        idx = 8;
    }
    else
        idx = 4;
    metadata_pointer->metadata_format = descriptor[idx];
    ++idx;
    if( metadata_pointer->metadata_format == 0xFF )
    {
        metadata_pointer->metadata_format_identifier = (descriptor[idx+0] << 24)
                                                     | (descriptor[idx+1] << 16)
                                                     | (descriptor[idx+2] <<  8)
                                                     |  descriptor[idx+3];
        idx += 4;
    }
    metadata_pointer->metadata_service_id          =    descriptor[idx];
    ++idx;
    metadata_pointer->metadata_locator_record_flag = !!(descriptor[idx] & 0x80);
    metadata_pointer->MPEG_carriage_flags          =   (descriptor[idx] & 0x60) >> 5;
    /* reserved                                    =    descriptor[idx] & 0x1F; */
    ++idx;
    if( metadata_pointer->metadata_locator_record_flag )
    {
        metadata_pointer->metadata_locator_record_length = descriptor[idx];
        ++idx;
        if( metadata_pointer->metadata_locator_record_length )
        {
            memcpy( metadata_pointer->metadata_locator_record_byte, &(descriptor[idx]), metadata_pointer->metadata_locator_record_length );
            idx += metadata_pointer->metadata_locator_record_length;
        }
    }
    if( metadata_pointer->MPEG_carriage_flags <= 2 )
    {
        metadata_pointer->program_number = (descriptor[idx+0] << 8) | descriptor[idx+1];
        idx += 2;
    }
    if( metadata_pointer->MPEG_carriage_flags == 1 )
    {
        metadata_pointer->transport_stream_location = (descriptor[idx+0] << 8) | descriptor[idx+1];
        metadata_pointer->ransport_stream_id        = (descriptor[idx+2] << 8) | descriptor[idx+3];
        idx += 4;
    }
    /* private_data_byte */
    metadata_pointer->private_data_byte_length = descriptor[1] - idx + 2;
    if( metadata_pointer->private_data_byte_length )
        memcpy( metadata_pointer->private_data_byte, &(descriptor[idx]), metadata_pointer->private_data_byte_length );
}

READ_DESCRIPTOR( metadata )
{
    metadata->metadata_application_format = (descriptor[2] << 8) | descriptor[3];
    uint8_t idx;
    if( metadata->metadata_application_format == 0xFFFF )
    {
        metadata->metadata_application_format_identifier = (descriptor[4] << 24)
                                                         | (descriptor[5] << 16)
                                                         | (descriptor[6] <<  8)
                                                         |  descriptor[7];
        idx = 8;
    }
    else
        idx = 4;
    metadata->metadata_format = descriptor[idx];
    ++idx;
    if( metadata->metadata_format == 0xFF )
    {
        metadata->metadata_format_identifier = (descriptor[idx+0] << 24)
                                             | (descriptor[idx+1] << 16)
                                             | (descriptor[idx+2] <<  8)
                                             |  descriptor[idx+3];
        idx += 4;
    }
    metadata->metadata_service_id  =  descriptor[idx];
    ++idx;
    metadata->decoder_config_flags = (descriptor[idx] & 0xE0) >> 5;
    metadata->DSM_CC_flag          = (descriptor[idx] & 0x10) >> 4;
    /* reserved                    =  descriptor[idx] & 0x0F; */
    ++idx;
    if( metadata->DSM_CC_flag )
    {
        metadata->service_identification_length = descriptor[idx];
        ++idx;
        if( metadata->service_identification_length )
        {
            memcpy( metadata->service_identification_record_byte, &(descriptor[idx]), metadata->service_identification_length );
            idx += metadata->service_identification_length;
        }
    }
    if( metadata->decoder_config_flags == 1 )
    {
        metadata->decoder_config_length = descriptor[idx];
        ++idx;
        if( metadata->decoder_config_length )
        {
            memcpy( metadata->decoder_config_byte, &(descriptor[idx]), metadata->decoder_config_length );
            idx += metadata->decoder_config_length;
        }
    }
    if( metadata->decoder_config_flags == 3 )
    {
        metadata->dec_config_identification_record_length = descriptor[idx];
        ++idx;
        if( metadata->dec_config_identification_record_length )
        {
            memcpy( metadata->dec_config_identification_record_byte, &(descriptor[idx]), metadata->dec_config_identification_record_length );
            idx += metadata->dec_config_identification_record_length;
        }
    }
    if( metadata->decoder_config_flags == 4 )
    {
        metadata->decoder_config_metadata_service_id = descriptor[idx];
        ++idx;
    }
    if( metadata->decoder_config_flags == 5 || metadata->decoder_config_flags == 6 )
    {
        metadata->reserved_data_length = descriptor[idx];
        ++idx;
        if( metadata->reserved_data_length )
        {
            memcpy( metadata->reserved_data, &(descriptor[idx]), metadata->reserved_data_length );
            idx += metadata->reserved_data_length;
        }
    }
    /* private_data_byte */
    metadata->private_data_byte_length = descriptor[1] - idx + 2;
    if( metadata->private_data_byte_length )
        memcpy( metadata->private_data_byte, &(descriptor[idx]), metadata->private_data_byte_length );
}

READ_DESCRIPTOR( metadata_STD )
{
    /* reserved                             = (descriptor[2] & 0xC0) >> 6; */
    metadata_STD->metadata_input_leak_rate  = (descriptor[2] & 0x3F) << 16
                                            |  descriptor[3]         <<  8
                                            |  descriptor[4];
    /* reserved                             = (descriptor[5] & 0xC0) >> 6; */
    metadata_STD->metadata_buffer_size      = (descriptor[5] & 0x3F) << 16
                                            |  descriptor[6]         <<  8
                                            |  descriptor[7];
    /* reserved                             = (descriptor[8] & 0xC0) >> 6; */
    metadata_STD->metadata_output_leak_rate = (descriptor[8] & 0x3F) << 16
                                            |  descriptor[9]         <<  8
                                            |  descriptor[10];
}

READ_DESCRIPTOR( AVC_video )
{
    AVC_video->profile_idc                        =    descriptor[2];
    AVC_video->constraint_set0_flag               = !!(descriptor[3] & 0x80);
    AVC_video->constraint_set1_flag               = !!(descriptor[3] & 0x40);
    AVC_video->constraint_set2_flag               = !!(descriptor[3] & 0x20);
    AVC_video->constraint_set3_flag               = !!(descriptor[3] & 0x10);
    AVC_video->constraint_set4_flag               = !!(descriptor[3] & 0x08);
    AVC_video->constraint_set5_flag               = !!(descriptor[3] & 0x04);
    AVC_video->AVC_compatible_flags               =    descriptor[3] & 0x03;
    AVC_video->level_idc                          =    descriptor[4];
    AVC_video->AVC_still_present                  = !!(descriptor[5] & 0x80);
    AVC_video->AVC_24_hour_picture_flag           = !!(descriptor[5] & 0x40);
    AVC_video->Frame_Packing_SEI_not_present_flag = !!(descriptor[5] & 0x20);
    /* reserved                                   =    descriptor[8] & 0x1F; */
}

READ_DESCRIPTOR( IPMP )
{
    IPMP->descriptor_length    = descriptor[1];
    if( IPMP->descriptor_length )
        memcpy( IPMP->descriptor_data, &(descriptor[2]), IPMP->descriptor_length );
}

READ_DESCRIPTOR( AVC_timing_and_HRD )
{
    AVC_timing_and_HRD->hrd_management_valid_flag       = !!(descriptor[2] & 0x80);
    /* reserved                                         =   (descriptor[2] & 0x7E) >> 1; */
    AVC_timing_and_HRD->picture_and_timing_info_present =    descriptor[2] & 0x01;
    uint8_t idx = 3;
    if( AVC_timing_and_HRD->picture_and_timing_info_present )
    {
        AVC_timing_and_HRD->_90kHz_flag        = !!(descriptor[3] & 0x80);
        /* reserved                            =   (descriptor[3] & 0x7F); */
        if( AVC_timing_and_HRD->_90kHz_flag )
        {
            AVC_timing_and_HRD->N = (descriptor[ 4] << 24)
                                  | (descriptor[ 5] << 16)
                                  | (descriptor[ 6] <<  8)
                                  |  descriptor[ 7];
            AVC_timing_and_HRD->K = (descriptor[ 8] << 24)
                                  | (descriptor[ 9] << 16)
                                  | (descriptor[10] <<  8)
                                  |  descriptor[11];
            idx = 12;
        }
        else
            idx = 4;
        AVC_timing_and_HRD->num_units_in_tick = descriptor[idx+0] << 24
                                              | descriptor[idx+1] << 16
                                              | descriptor[idx+2] <<  8
                                              | descriptor[idx+3];
    }
    AVC_timing_and_HRD->fixed_frame_rate_flag              = !!(descriptor[idx] & 0x80);
    AVC_timing_and_HRD->temporal_poc_flag                  = !!(descriptor[idx] & 0x40);
    AVC_timing_and_HRD->picture_to_display_conversion_flag = !!(descriptor[idx] & 0x20);
    /* reserved                                            =    descriptor[idx] & 0x1F; */
}

READ_DESCRIPTOR( MPEG2_AAC_audio )
{
    MPEG2_AAC_audio->MPEG2_AAC_profile                = descriptor[2];
    MPEG2_AAC_audio->MPEG2_AAC_channel_configuration  = descriptor[3];
    MPEG2_AAC_audio->MPEG2_AAC_additional_information = descriptor[4];
}

READ_DESCRIPTOR( FlexMuxTiming )
{
    FlexMuxTiming->FCR_ES_ID     = (descriptor[2] << 8) | descriptor[3];
    FlexMuxTiming->FCRResolution = (descriptor[4] << 24)
                                 | (descriptor[5] << 16)
                                 | (descriptor[6] <<  8)
                                 |  descriptor[7];
    FlexMuxTiming->FCRLength     =  descriptor[8];
    FlexMuxTiming->FmxRateLength =  descriptor[9];
}

READ_DESCRIPTOR( MPEG4_text )       // FIXME (data size is big.)
{
 // TextConfig();               /* defined in ISO/IEC 14496-17. */
    MPEG4_text->textFormat       =  descriptor[2];
    MPEG4_text->textConfigLength = (descriptor[3] << 8) | descriptor[4];
 // formatSpecificTextConfig();
    MPEG4_text->_3GPPBaseFormat  =  descriptor[5];
    MPEG4_text->profileLevel     =  descriptor[6];
    MPEG4_text->durationClock    = (descriptor[7] << 16)
                                 | (descriptor[8] <<  8)
                                 |  descriptor[9];
    MPEG4_text->contains_list_of_compatible_3GPPFormats_flag = !!(descriptor[10] & 0x80);
    MPEG4_text->sampleDescriptionFlags                       =   (descriptor[10] & 0x60) >> 5;
    MPEG4_text->SampleDescription_carriage_flag              = !!(descriptor[10] & 0x10);
    MPEG4_text->positioning_information_flag                 = !!(descriptor[10] & 0x08);
    /* reserved                                              =    descriptor[10] & 0x07; */
    MPEG4_text->layer             =  descriptor[11];
    MPEG4_text->text_track_width  = (descriptor[12] << 8) | descriptor[13];
    MPEG4_text->text_track_height = (descriptor[14] << 8) | descriptor[15];
    uint8_t idx = 16;
    if( MPEG4_text->contains_list_of_compatible_3GPPFormats_flag )
    {
        MPEG4_text->number_of_formats = descriptor[16];
        if( MPEG4_text->number_of_formats )
        {
            memcpy( MPEG4_text->Compatible_3GPPFormat, &(descriptor[17]), MPEG4_text->number_of_formats );
            idx = 17 + MPEG4_text->number_of_formats;
        }
        else
            idx = 17;
    }
    if( MPEG4_text->SampleDescription_carriage_flag )
    {
        MPEG4_text->number_of_SampleDescriptions = descriptor[idx];
        ++idx;
     // Sample_index_and_description()(number-of-SampleDescriptions);
        for( uint8_t i = 0; i < MPEG4_text->number_of_SampleDescriptions; ++i )
        {
            MPEG4_text->Sample_index_and_description[i].sample_index = descriptor[idx];
            ++idx;
         // SampleDescription();    /* specified in 3GPP TS 26.245. */
            MPEG4_text->Sample_index_and_description[i].displayFlags             = (descriptor[idx+0] << 24)
                                                                                 | (descriptor[idx+1] << 16)
                                                                                 | (descriptor[idx+2] <<  8)
                                                                                 |  descriptor[idx+3];
            MPEG4_text->Sample_index_and_description[i].horizontal_justification = (int8_t)descriptor[idx+4];
            MPEG4_text->Sample_index_and_description[i].vertical_justification   = (int8_t)descriptor[idx+5];
            MPEG4_text->Sample_index_and_description[i].background_color_rgba[0] = descriptor[idx+6];
            MPEG4_text->Sample_index_and_description[i].background_color_rgba[1] = descriptor[idx+7];
            MPEG4_text->Sample_index_and_description[i].background_color_rgba[2] = descriptor[idx+8];
            MPEG4_text->Sample_index_and_description[i].background_color_rgba[3] = descriptor[idx+9];
            idx += 10;
         // BoxRecord       default-text-box;
            MPEG4_text->Sample_index_and_description[i].default_text_box.top    = (int16_t)((descriptor[idx+0] << 8) | descriptor[idx+1]);
            MPEG4_text->Sample_index_and_description[i].default_text_box.left   = (int16_t)((descriptor[idx+2] << 8) | descriptor[idx+3]);
            MPEG4_text->Sample_index_and_description[i].default_text_box.bottom = (int16_t)((descriptor[idx+4] << 8) | descriptor[idx+5]);
            MPEG4_text->Sample_index_and_description[i].default_text_box.right  = (int16_t)((descriptor[idx+6] << 8) | descriptor[idx+7]);
            idx += 8;
         // StyleRecord     default-style;
            MPEG4_text->Sample_index_and_description[i].default_style.startChar          = (descriptor[idx+0] << 8) | descriptor[idx+1];
            MPEG4_text->Sample_index_and_description[i].default_style.endChar            = (descriptor[idx+2] << 8) | descriptor[idx+3];
            MPEG4_text->Sample_index_and_description[i].default_style.font_ID            = (descriptor[idx+4] << 8) | descriptor[idx+5];
            MPEG4_text->Sample_index_and_description[i].default_style.face_style_flags   =  descriptor[idx+6];
            MPEG4_text->Sample_index_and_description[i].default_style.font_size          =  descriptor[idx+7];
            MPEG4_text->Sample_index_and_description[i].default_style.text_color_rgba[0] =  descriptor[idx+8];
            MPEG4_text->Sample_index_and_description[i].default_style.text_color_rgba[1] =  descriptor[idx+9];
            MPEG4_text->Sample_index_and_description[i].default_style.text_color_rgba[2] =  descriptor[idx+10];
            MPEG4_text->Sample_index_and_description[i].default_style.text_color_rgba[3] =  descriptor[idx+11];
            idx += 12;
         // FontTableBox    font-table;
            MPEG4_text->Sample_index_and_description[i].font_table.entry_count = (descriptor[idx+0] << 8) | descriptor[idx+1];
            ++idx;
         // FontRecord    font-entry[entry-count];
            for( uint8_t j = 0; j < MPEG4_text->Sample_index_and_description[i].font_table.entry_count; ++j )
            {
                MPEG4_text->Sample_index_and_description[i].font_table.font_entry[j].font_ID          = (descriptor[idx+2] << 8) | descriptor[idx+3];
                MPEG4_text->Sample_index_and_description[i].font_table.font_entry[j].font_name_length =  descriptor[idx+4];
                if( MPEG4_text->Sample_index_and_description[i].font_table.font_entry[j].font_name_length )
                {
                    memcpy( MPEG4_text->Sample_index_and_description[i].font_table.font_entry[j].font, &(descriptor[idx+5])
                          , MPEG4_text->Sample_index_and_description[i].font_table.font_entry[j].font_name_length );
                    idx += 3 + MPEG4_text->Sample_index_and_description[i].font_table.font_entry[j].font_name_length;
                }
                else
                    idx += 3;
            }
        }
    }
    if( MPEG4_text->positioning_information_flag )
    {
        MPEG4_text->scene_width             = (descriptor[idx+0] << 8) | descriptor[idx+1];
        MPEG4_text->scene_height            = (descriptor[idx+2] << 8) | descriptor[idx+3];
        MPEG4_text->horizontal_scene_offset = (descriptor[idx+4] << 8) | descriptor[idx+5];
        MPEG4_text->vertical_scene_offset   = (descriptor[idx+6] << 8) | descriptor[idx+7];
    }
}

READ_DESCRIPTOR( MPEG4_audio_extension )
{
    MPEG4_audio_extension->ASC_flag     = !!(descriptor[2] & 0x80);
    /* reserved                         =   (descriptor[2] & 0x70) >> 4; */
    MPEG4_audio_extension->num_of_loops =    descriptor[2] & 0x0F;
    for( uint8_t i = 0; i < MPEG4_audio_extension->num_of_loops; ++i )
        MPEG4_audio_extension->audioProfileLevelIndication[i] = descriptor[3 + i];
    uint8_t idx = 3 + MPEG4_audio_extension->num_of_loops;
    if( MPEG4_audio_extension->ASC_flag )
    {
        MPEG4_audio_extension->ASC_size = descriptor[idx];
        ++idx;
     // audioSpecificConfig();      /* specified in 1.6.2.1 in ISO/IEC 14496-3. */      // FIXME
        if( MPEG4_audio_extension->ASC_size )
            memcpy( MPEG4_audio_extension->audioSpecificConfig, &(descriptor[idx]), MPEG4_audio_extension->ASC_size );
    }
}

READ_DESCRIPTOR( Auxiliary_video_stream )
{
    Auxiliary_video_stream->aux_video_codedstreamtype = descriptor[2];
 // si_rbsp(descriptor_length-1);
    Auxiliary_video_stream->si_rbsp_length = descriptor[1] - 1;
    if( Auxiliary_video_stream->si_rbsp_length )
        memcpy( Auxiliary_video_stream->si_rbsp, &(descriptor[3]), Auxiliary_video_stream->si_rbsp_length );
}

READ_DESCRIPTOR( SVC_extension )
{
    SVC_extension->width                   =   (descriptor[ 2] << 8) | descriptor[ 3];
    SVC_extension->height                  =   (descriptor[ 4] << 8) | descriptor[ 5];
    SVC_extension->frame_rate              =   (descriptor[ 6] << 8) | descriptor[ 7];
    SVC_extension->average_bitrate         =   (descriptor[ 8] << 8) | descriptor[ 9];
    SVC_extension->maximum_bitrate         =   (descriptor[10] << 8) | descriptor[10];
    SVC_extension->dependency_id           =   (descriptor[11] & 0xE0) >> 5;
    /* reserved                            =    descriptor[11] & 0x1F; */
    SVC_extension->quality_id_start        =   (descriptor[12] & 0xF0) >> 4;
    SVC_extension->quality_id_end          =    descriptor[12] & 0x0F;
    SVC_extension->temporal_id_start       =   (descriptor[13] & 0xE0) >> 5;
    SVC_extension->temporal_id_end         =   (descriptor[13] & 0x1C) >> 2;
    SVC_extension->no_sei_nal_unit_present = !!(descriptor[13] & 0x02);
    /* reserved                            =    descriptor[13] & 0x01; */
}

READ_DESCRIPTOR( MVC_extension )
{
    MVC_extension->average_bitrate              =   (descriptor[2] << 8) | descriptor[3];
    MVC_extension->maximum_bitrate              =   (descriptor[4] << 8) | descriptor[5];
    MVC_extension->view_association_not_present = !!(descriptor[6] & 0x80);
    MVC_extension->base_view_is_left_eyeview    = !!(descriptor[6] & 0x40);
    /* reserved                                 =   (descriptor[6] & 0x30) >> 4; */
    MVC_extension->view_order_index_min         =   (descriptor[6] & 0x0F) << 6 | (descriptor[7] & 0xFC) >> 2;
    MVC_extension->view_order_index_max         =   (descriptor[7] & 0x03) << 8 |  descriptor[8];
    MVC_extension->temporal_id_start            =   (descriptor[9] & 0xE0) >> 5;
    MVC_extension->temporal_id_end              =   (descriptor[9] & 0x1C) >> 2;
    MVC_extension->no_sei_nal_unit_present      = !!(descriptor[9] & 0x02);
    MVC_extension->no_prefix_nal_unit_present   =    descriptor[9] & 0x01;
}

READ_DESCRIPTOR( J2K_video )
{
    J2K_video->profile_and_level    =   (descriptor[ 2] << 8) | descriptor[ 3];
    J2K_video->horizontal_size      =   (descriptor[ 4] << 24)
                                    |   (descriptor[ 5] << 16)
                                    |   (descriptor[ 6] <<  8)
                                    |    descriptor[ 7];
    J2K_video->vertical_size        =   (descriptor[ 8] << 24)
                                    |   (descriptor[ 9] << 16)
                                    |   (descriptor[10] <<  8)
                                    |    descriptor[11];
    J2K_video->max_bit_rate         =   (descriptor[12] << 24)
                                    |   (descriptor[13] << 16)
                                    |   (descriptor[14] <<  8)
                                    |    descriptor[15];
    J2K_video->max_buffer_size      =   (descriptor[16] << 24)
                                    |   (descriptor[17] << 16)
                                    |   (descriptor[18] <<  8)
                                    |    descriptor[19];
    J2K_video->DEN_frame_rate       =   (descriptor[20] << 8) | descriptor[21];
    J2K_video->NUM_frame_rate       =   (descriptor[22] << 8) | descriptor[23];
    J2K_video->color_specification  =    descriptor[24];
    J2K_video->still_mode           = !!(descriptor[25] & 0x80);
    J2K_video->interlaced_video     = !!(descriptor[25] & 0x40);
    /* reserved                     =    descriptor[25] & 0x2F; */
    /* private_data_byte */
    J2K_video->private_data_byte_length = descriptor[1] - 26 + 2;
    if( J2K_video->private_data_byte_length )
        memcpy( J2K_video->private_data_byte, &(descriptor[26]), J2K_video->private_data_byte_length );
}

READ_DESCRIPTOR( MVC_operation_point )      // FIXME (data size is big.)
{
    MVC_operation_point->profile_idc          =    descriptor[2];
    MVC_operation_point->constraint_set0_flag = !!(descriptor[3] & 0x80);
    MVC_operation_point->constraint_set1_flag = !!(descriptor[3] & 0x40);
    MVC_operation_point->constraint_set2_flag = !!(descriptor[3] & 0x20);
    MVC_operation_point->constraint_set3_flag = !!(descriptor[3] & 0x10);
    MVC_operation_point->constraint_set4_flag = !!(descriptor[3] & 0x08);
    MVC_operation_point->constraint_set5_flag = !!(descriptor[3] & 0x04);
    MVC_operation_point->AVC_compatible_flags =    descriptor[3] & 0x03;
    MVC_operation_point->level_count          =    descriptor[4];
    uint8_t idx = 5;
    for( uint8_t i = 0; i < MVC_operation_point->level_count; ++i )
    {
        MVC_operation_point->levels[i].level_idc               = descriptor[idx+0];
        MVC_operation_point->levels[i].operation_points_count  = descriptor[idx+1];
        idx += 2;
        for( uint8_t j = 0; j < MVC_operation_point->levels[i].operation_points_count; ++j )
        {
            /* reserved                                                                = !!(descriptor[idx+0] & 0xF8) >> 3; */
            MVC_operation_point->levels[i].operation_points[j].applicable_temporal_id  =    descriptor[idx+0] & 0x07;
            MVC_operation_point->levels[i].operation_points[j].num_target_output_views =    descriptor[idx+1];
            MVC_operation_point->levels[i].operation_points[j].ES_count                =    descriptor[idx+2];
            idx += 3;
            for( uint8_t k = 0; k < MVC_operation_point->levels[i].operation_points[j].ES_count; ++k )
            {
                /* reserved                                                        = !!(descriptor[idx] & 0xC0) >> 6; */
                MVC_operation_point->levels[i].operation_points[j].ES_reference[k] =    descriptor[idx] & 0xC3F;
                ++idx;
            }
        }
    }
}

READ_DESCRIPTOR( MPEG2_stereoscopic_video_format )
{
    MPEG2_stereoscopic_video_format->stereo_video_arrangement_type_present = !!(descriptor[2] & 0x80);
    if( MPEG2_stereoscopic_video_format->stereo_video_arrangement_type_present )
        MPEG2_stereoscopic_video_format->arrangement_type                  =    descriptor[2] & 0x7F;
}

READ_DESCRIPTOR( Stereoscopic_program_info )
{
    /* reserved                                          = (descriptor[2] & 0xF8) >> 3; */
    Stereoscopic_program_info->stereoscopic_service_type =  descriptor[2] & 0x07;
}

READ_DESCRIPTOR( Stereoscopic_video_info )
{
    /* reserved                                               = (descriptor[2] & 0xFE) >> 1; */
    Stereoscopic_video_info->base_video_flag                  =  descriptor[2] & 0x01;
    if( Stereoscopic_video_info->base_video_flag )
    {
        /* reserved                                           = (descriptor[3] & 0xFE) >> 1; */
        Stereoscopic_video_info->leftview_flag                =  descriptor[3] & 0x01;
    }
    else
    {
        /* reserved                                           = (descriptor[3] & 0xFE) >> 1; */
        Stereoscopic_video_info->usable_as_2D                 =  descriptor[3] & 0x01;
        Stereoscopic_video_info->horizontal_upsampling_factor = (descriptor[4] & 0xF0) >> 4;
        Stereoscopic_video_info->vertical_upsampling_factor   =  descriptor[4] & 0x0F;
    }
}

READ_DESCRIPTOR( Transport_profile )
{
    Transport_profile->transport_profile = descriptor[2];
    /* private_data */
    Transport_profile->private_data_length = descriptor[1] - 3 + 2;
    if( Transport_profile->private_data_length )
        memcpy( Transport_profile->private_data, &(descriptor[3]), Transport_profile->private_data_length );
}

READ_DESCRIPTOR( HEVC_video )
{
    HEVC_video->profile_space                       =   (descriptor[2] & 0xC0) >> 6;
    HEVC_video->tier_flag                           = !!(descriptor[2] & 0x20);
    HEVC_video->profile_idc                         =    descriptor[2] & 0x1F;
    HEVC_video->profile_compatibility_indication    =   (descriptor[3] << 24)
                                                    |   (descriptor[4] << 16)
                                                    |   (descriptor[5] <<  8)
                                                    |    descriptor[6];
    HEVC_video->progressive_source_flag             = !!(descriptor[7] & 0x80);
    HEVC_video->interlaced_source_flag              = !!(descriptor[7] & 0x40);
    HEVC_video->non_packed_constraint_flag          = !!(descriptor[7] & 0x20);
    HEVC_video->frame_only_constraint_flag          = !!(descriptor[7] & 0x10);
    HEVC_video->copied_44bits                       = ((uint64_t)(descriptor[7] & 0x0F) << 36)
                                                    | ((uint64_t) descriptor[8]         << 32)
                                                    | (           descriptor[9]         << 24)
                                                    | (           descriptor[10]        << 16)
                                                    | (           descriptor[11]        <<  8)
                                                    |             descriptor[12];
    HEVC_video->level_idc                           =    descriptor[13];
    HEVC_video->temporal_layer_subset_flag          = !!(descriptor[14] & 0x80);
    HEVC_video->HEVC_still_present_flag             = !!(descriptor[14] & 0x40);
    HEVC_video->HEVC_24hr_picture_present_flag      = !!(descriptor[14] & 0x20);
    HEVC_video->sub_pic_hrd_params_not_present_flag = !!(descriptor[14] & 0x10);
    /* reserved                                     =   (descriptor[14] & 0x0C) >> 2; */
    HEVC_video->HDR_WCG_idc                         =    descriptor[14] & 0x03;
    if( HEVC_video->temporal_layer_subset_flag )
    {
        HEVC_video->temporal_id_min                 =   (descriptor[15] & 0xE0) >> 5;
        /* reserved                                 =    descriptor[15] & 0x1F; */
        HEVC_video->temporal_id_max                 =   (descriptor[16] & 0xE0) >> 5;
        /* reserved                                 =    descriptor[16] & 0x1F; */
    }
}

READ_DESCRIPTOR( Extension )
{
    Extension->extension_descriptor_tag = descriptor[2];
    /* extension_descriptor_data */         // FIXME
    Extension->extension_descriptor_length = descriptor[1] - 3 + 2;
    if( Extension->extension_descriptor_length )
        memcpy( Extension->extension_descriptor_data, &(descriptor[3]), Extension->extension_descriptor_length );
}

/*  */
READ_DESCRIPTOR( component )
{
    component->component_tag = descriptor[2];
}

READ_DESCRIPTOR( stream_identifier )
{
    stream_identifier->component_tag = descriptor[2];
}

READ_DESCRIPTOR( CA_identifier )
{
    CA_identifier->CA_system_id = (descriptor[2] << 8) | descriptor[3];
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
    descriptor_info->tags[descriptor_info->tags_num] = descriptor[0];
    descriptor_info->length                          = descriptor[1];
    switch( descriptor_info->tags[descriptor_info->tags_num] )
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
        EXECUTE_READ_DESCRIPTOR( content_labeling )
        EXECUTE_READ_DESCRIPTOR( metadata_pointer )
        EXECUTE_READ_DESCRIPTOR( metadata )
        EXECUTE_READ_DESCRIPTOR( metadata_STD )
        EXECUTE_READ_DESCRIPTOR( AVC_video )
        EXECUTE_READ_DESCRIPTOR( IPMP )
        EXECUTE_READ_DESCRIPTOR( AVC_timing_and_HRD )
        EXECUTE_READ_DESCRIPTOR( MPEG2_AAC_audio )
        EXECUTE_READ_DESCRIPTOR( FlexMuxTiming )
        EXECUTE_READ_DESCRIPTOR( MPEG4_text )
        EXECUTE_READ_DESCRIPTOR( MPEG4_audio_extension )
        EXECUTE_READ_DESCRIPTOR( Auxiliary_video_stream )
        EXECUTE_READ_DESCRIPTOR( SVC_extension )
        EXECUTE_READ_DESCRIPTOR( MVC_extension )
        EXECUTE_READ_DESCRIPTOR( J2K_video )
        EXECUTE_READ_DESCRIPTOR( MVC_operation_point )
        EXECUTE_READ_DESCRIPTOR( MPEG2_stereoscopic_video_format )
        EXECUTE_READ_DESCRIPTOR( Stereoscopic_program_info )
        EXECUTE_READ_DESCRIPTOR( Stereoscopic_video_info )
        EXECUTE_READ_DESCRIPTOR( Transport_profile )
        EXECUTE_READ_DESCRIPTOR( HEVC_video )
        EXECUTE_READ_DESCRIPTOR( Extension )
        /*  */
        EXECUTE_READ_DESCRIPTOR( component )
        EXECUTE_READ_DESCRIPTOR( stream_identifier )
        EXECUTE_READ_DESCRIPTOR( CA_identifier )
        default :
            break;
    }
    ++ descriptor_info->tags_num;
    descriptor_info->tags[descriptor_info->tags_num] = 0;
}
#undef EXECUTE_READ_DESCRIPTOR

#define PRINT_DESCRIPTOR_INFO( name, ... )          \
{                                                   \
    case name##_descriptor :                        \
        mapi_log( LOG_LV2,                          \
                  "[check] "#name"_descriptor\n"    \
                  __VA_ARGS__ );                    \
}
extern void mpeg_stream_debug_descriptor_info( mpeg_descriptor_info_t *descriptor_info, uint16_t descriptor_num )
{
    switch( descriptor_info->tags[descriptor_num] )
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
                "        format_identifier:0x%08X\n"
                , descriptor_info->registration.format_identifier
            )
            /* additional_identification_info */
#ifdef DEBUG
            if( descriptor_info->registration.additional_identification_info_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->registration.additional_identification_info_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->registration.additional_identification_info[i] );
                buf[descriptor_info->registration.additional_identification_info_length * 2] = '\0';
                mapi_log( LOG_LV2, "        additional_identification_info:0x%s\n", buf );
            }
#endif
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
            /* private_data_byte */
#ifdef DEBUG
            if( descriptor_info->conditional_access.private_data_byte_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->conditional_access.private_data_byte_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->conditional_access.private_data_byte[i] );
                buf[descriptor_info->conditional_access.private_data_byte_length * 2] = '\0';
                mapi_log( LOG_LV2, "        private_data_byte:0x%s\n", buf );
            }
#endif
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
            /* additional_copyright_info */
#ifdef DEBUG
            if( descriptor_info->copyright.additional_copyright_info_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->copyright.additional_copyright_info_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->copyright.additional_copyright_info[i] );
                buf[descriptor_info->copyright.additional_copyright_info_length * 2] = '\0';
                mapi_log( LOG_LV2, "        additional_copyright_info:0x%s\n", buf );
            }
#endif
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
                , descriptor_info->IOD.Scope_of_IOD_label
                , descriptor_info->IOD.IOD_label
            )
            /* InitialObjectDescriptor() */         // FIXME
#ifdef DEBUG
            if( descriptor_info->IOD.InitialObjectDescriptor_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->IOD.InitialObjectDescriptor_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->IOD.InitialObjectDescriptor_data[i] );
                buf[descriptor_info->IOD.InitialObjectDescriptor_length * 2] = '\0';
                mapi_log( LOG_LV2, "        InitialObjectDescriptor_data:0x%s\n", buf );
            }
#endif
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
        PRINT_DESCRIPTOR_INFO( MuxCode,
                "        length:%u\n"
                "        MuxCode:%u\n"
                "        version:%u\n"
                "        substructureCount:%u\n"
                , descriptor_info->MuxCode.length
                , descriptor_info->MuxCode.MuxCode
                , descriptor_info->MuxCode.version
                , descriptor_info->MuxCode.substructureCount
            )
            for( uint8_t i = 0; i < descriptor_info->MuxCode.substructureCount; ++i )
            {
                mapi_log( LOG_LV2, "          slotCount[%u]:%u"
                                   "          repetitionCount[%u]:%u"
                                 , i, descriptor_info->MuxCode.subs[i].slotCount
                                 , i, descriptor_info->MuxCode.subs[i].repetitionCount );
                for( uint8_t k = 0; k < descriptor_info->MuxCode.subs[i].slotCount; ++k )
                {
                    mapi_log( LOG_LV2, "            flexMuxChannel[%u][%u]:%u"
                                       "            numberOfBytes[%u][%u]:%u"
                                     , i, k, descriptor_info->MuxCode.subs[i].slots[k].flexMuxChannel
                                     , i, k, descriptor_info->MuxCode.subs[i].slots[k].numberOfBytes  );
                }
            }
            break;
        PRINT_DESCRIPTOR_INFO( FmxBufferSize )
            /* DefaultFlexMuxBufferDescriptor() & FlexMuxBufferDescriptor() */      // FIXME
#ifdef DEBUG
            if( descriptor_info->FmxBufferSize.descriptor_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->FmxBufferSize.descriptor_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->FmxBufferSize.descriptor_data[i] );
                buf[descriptor_info->FmxBufferSize.descriptor_length * 2] = '\0';
                mapi_log( LOG_LV2, "        descriptor_data:0x%s\n", buf );
            }
#endif
            break;
        PRINT_DESCRIPTOR_INFO( MultiplexBuffer,
                "        MB_buffer_size:%u\n"
                "        TB_leak_rate:%u\n"
                , descriptor_info->MultiplexBuffer.MB_buffer_size
                , descriptor_info->MultiplexBuffer.TB_leak_rate
            )
            break;
        PRINT_DESCRIPTOR_INFO( content_labeling,
                "        metadata_application_format:0x%04X\n"
                , descriptor_info->content_labeling.metadata_application_format
            )
            if( descriptor_info->content_labeling.metadata_application_format == 0xFFFF )
                mapi_log( LOG_LV2, "          metadata_application_format_identifier:0x%08X\n"
                                 , descriptor_info->content_labeling.metadata_application_format_identifier );
            mapi_log( LOG_LV2, "        content_reference_id_record_flag:%u\n"
                               "        content_time_base_indicator:%u\n"
                             , descriptor_info->content_labeling.content_reference_id_record_flag
                             , descriptor_info->content_labeling.content_time_base_indicator );
            if( descriptor_info->content_labeling.content_reference_id_record_flag
             && descriptor_info->content_labeling.content_reference_id_record_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->content_labeling.content_reference_id_record_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->content_labeling.content_reference_id_byte[i] );
                buf[descriptor_info->content_labeling.content_reference_id_record_length * 2] = '\0';
                mapi_log( LOG_LV2, "          content_reference_id_byte:0x%s\n", buf );
            }
            if( descriptor_info->content_labeling.content_time_base_indicator == 1
             || descriptor_info->content_labeling.content_time_base_indicator == 2 )
                mapi_log( LOG_LV2, "          content_time_base_value:%" PRIu64 "\n"
                                   "          metadata_time_base_value:%" PRIu64 "\n"
                                 , descriptor_info->content_labeling.content_time_base_value
                                 , descriptor_info->content_labeling.metadata_time_base_value );
            if( descriptor_info->content_labeling.content_time_base_indicator == 2 )
                mapi_log( LOG_LV2, "          contentId:%u\n", descriptor_info->content_labeling.contentId );
            if( 3 <= descriptor_info->content_labeling.content_time_base_indicator
             && descriptor_info->content_labeling.content_time_base_indicator <= 7
             && descriptor_info->content_labeling.time_base_association_data_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->content_labeling.time_base_association_data_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->content_labeling.time_base_association_data[i] );
                buf[descriptor_info->content_labeling.time_base_association_data_length * 2] = '\0';
                mapi_log( LOG_LV2, "          time_base_association_data:0x%s\n", buf );
            }
            break;
        PRINT_DESCRIPTOR_INFO( metadata_pointer,
                "        metadata_application_format:0x%04X\n"
                , descriptor_info->metadata_pointer.metadata_application_format
            )
            if( descriptor_info->metadata_pointer.metadata_application_format == 0xFFFF )
                mapi_log( LOG_LV2, "          metadata_application_format_identifier:0x%08X\n"
                                 , descriptor_info->metadata_pointer.metadata_application_format_identifier );
            mapi_log( LOG_LV2, "        metadata_format:0x%02X\n"
                             , descriptor_info->metadata_pointer.metadata_format );
            if( descriptor_info->metadata_pointer.metadata_format == 0xFF )
                mapi_log( LOG_LV2, "          metadata_format_identifier:0x%08X\n"
                                 , descriptor_info->metadata_pointer.metadata_format_identifier );
            mapi_log( LOG_LV2, "        metadata_service_id:%u\n"
                               "        metadata_locator_record_flag:%u\n"
                               "        MPEG_carriage_flags:%u\n"
                             , descriptor_info->metadata_pointer.metadata_service_id
                             , descriptor_info->metadata_pointer.metadata_locator_record_flag
                             , descriptor_info->metadata_pointer.MPEG_carriage_flags );
            if( descriptor_info->metadata_pointer.metadata_locator_record_flag
             && descriptor_info->metadata_pointer.metadata_locator_record_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->metadata_pointer.metadata_locator_record_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->metadata_pointer.metadata_locator_record_byte[i] );
                buf[descriptor_info->metadata_pointer.metadata_locator_record_length * 2] = '\0';
                mapi_log( LOG_LV2, "          metadata_locator_record_byte:0x%s\n", buf );
            }
            if( descriptor_info->metadata_pointer.MPEG_carriage_flags <= 2 )
                mapi_log( LOG_LV2, "          program_number:0x%04X\n", descriptor_info->metadata_pointer.program_number );
            if( descriptor_info->metadata_pointer.MPEG_carriage_flags == 1 )
                mapi_log( LOG_LV2, "          transport_stream_location:0x%04X\n"
                                   "          ransport_stream_id:0x%04X\n"
                                 , descriptor_info->metadata_pointer.transport_stream_location
                                 , descriptor_info->metadata_pointer.ransport_stream_id );
            /* private_data_byte */
#ifdef DEBUG
            if( descriptor_info->metadata_pointer.private_data_byte_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->metadata_pointer.private_data_byte_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->metadata_pointer.private_data_byte[i] );
                buf[descriptor_info->metadata_pointer.private_data_byte_length * 2] = '\0';
                mapi_log( LOG_LV2, "          private_data_byte:0x%s\n", buf );
            }
#endif
            break;
        PRINT_DESCRIPTOR_INFO( metadata,
                "        metadata_application_format:0x%04X\n"
                , descriptor_info->metadata.metadata_application_format
            )
            if( descriptor_info->metadata.metadata_application_format == 0xFFFF )
                mapi_log( LOG_LV2, "          metadata_application_format_identifier:0x%08X\n"
                                 , descriptor_info->metadata.metadata_application_format_identifier );
            mapi_log( LOG_LV2, "        metadata_format:%u\n", descriptor_info->metadata_pointer.metadata_format );
            if( descriptor_info->metadata.metadata_format == 0xFF )
                mapi_log( LOG_LV2, "          metadata_format_identifier:0x%08X\n"
                                 , descriptor_info->metadata.metadata_format_identifier );
            mapi_log( LOG_LV2, "        metadata_service_id:%u\n"
                               "        decoder_config_flags:%u\n"
                               "        DSM_CC_flag:%u\n"
                             , descriptor_info->metadata.metadata_service_id
                             , descriptor_info->metadata.decoder_config_flags
                             , descriptor_info->metadata.DSM_CC_flag );
            if( descriptor_info->metadata.DSM_CC_flag
             && descriptor_info->metadata.service_identification_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->metadata.service_identification_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->metadata.service_identification_record_byte[i] );
                buf[descriptor_info->metadata.service_identification_length * 2] = '\0';
                mapi_log( LOG_LV2, "          service_identification_record_byte:0x%s\n", buf );
            }
            if( descriptor_info->metadata.decoder_config_flags == 1
             && descriptor_info->metadata.decoder_config_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->metadata.decoder_config_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->metadata.decoder_config_byte[i] );
                buf[descriptor_info->metadata.decoder_config_length * 2] = '\0';
                mapi_log( LOG_LV2, "          decoder_config_byte:0x%s\n", buf );
            }
            if( descriptor_info->metadata.decoder_config_flags == 3
             && descriptor_info->metadata.dec_config_identification_record_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->metadata.dec_config_identification_record_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->metadata.dec_config_identification_record_byte[i] );
                buf[descriptor_info->metadata.dec_config_identification_record_length * 2] = '\0';
                mapi_log( LOG_LV2, "          dec_config_identification_record_byte:0x%s\n", buf );
            }
            if( descriptor_info->metadata.decoder_config_flags == 3 )
                mapi_log( LOG_LV2, "          decoder_config_metadata_service_id:%u\n", descriptor_info->metadata.decoder_config_metadata_service_id );

            if( (descriptor_info->metadata.decoder_config_flags == 5 || descriptor_info->metadata.decoder_config_flags == 6)
             &&  descriptor_info->metadata.reserved_data_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->metadata.reserved_data_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->metadata.reserved_data[i] );
                buf[descriptor_info->metadata.reserved_data_length * 2] = '\0';
                mapi_log( LOG_LV2, "          reserved_data:0x%s\n", buf );
            }
            /* private_data_byte */
#ifdef DEBUG
            if( descriptor_info->metadata.private_data_byte_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->metadata.private_data_byte_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->metadata.private_data_byte[i] );
                buf[descriptor_info->metadata.private_data_byte_length * 2] = '\0';
                mapi_log( LOG_LV2, "          private_data_byte:0x%s\n", buf );
            }
#endif
            break;
        PRINT_DESCRIPTOR_INFO( metadata_STD,
                "        metadata_input_leak_rate:%u\n"
                "        metadata_buffer_size:%u\n"
                "        metadata_output_leak_rate:%u\n"
                , descriptor_info->metadata_STD.metadata_input_leak_rate
                , descriptor_info->metadata_STD.metadata_buffer_size
                , descriptor_info->metadata_STD.metadata_output_leak_rate
            )
            break;
        PRINT_DESCRIPTOR_INFO( AVC_video,
                "        profile_idc:%u\n"
                "        constraint_set0_flag:%u\n"
                "        constraint_set1_flag:%u\n"
                "        constraint_set2_flag:%u\n"
                "        constraint_set3_flag:%u\n"
                "        constraint_set4_flag:%u\n"
                "        constraint_set5_flag:%u\n"
                "        AVC_compatible_flags:%u\n"
                "        level_idc:%u\n"
                "        AVC_still_present:%u\n"
                "        AVC_24_hour_picture_flag:%u\n"
                "        Frame_Packing_SEI_not_present_flag:%u\n"
                , descriptor_info->AVC_video.profile_idc
                , descriptor_info->AVC_video.constraint_set0_flag
                , descriptor_info->AVC_video.constraint_set1_flag
                , descriptor_info->AVC_video.constraint_set2_flag
                , descriptor_info->AVC_video.constraint_set3_flag
                , descriptor_info->AVC_video.constraint_set4_flag
                , descriptor_info->AVC_video.constraint_set5_flag
                , descriptor_info->AVC_video.AVC_compatible_flags
                , descriptor_info->AVC_video.level_idc
                , descriptor_info->AVC_video.AVC_still_present
                , descriptor_info->AVC_video.AVC_24_hour_picture_flag
                , descriptor_info->AVC_video.Frame_Packing_SEI_not_present_flag
            )
            break;
        PRINT_DESCRIPTOR_INFO( IPMP )
#ifdef DEBUG
            if( descriptor_info->IPMP.descriptor_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->IPMP.descriptor_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->IPMP.descriptor_data[i] );
                buf[descriptor_info->IPMP.descriptor_length * 2] = '\0';
                mapi_log( LOG_LV2, "          descriptor_data:0x%s\n", buf );
            }
#endif
            break;
        PRINT_DESCRIPTOR_INFO( AVC_timing_and_HRD,
                "        hrd_management_valid_flag:%u\n"
                "        picture_and_timing_info_present:%u\n"
                , descriptor_info->AVC_timing_and_HRD.hrd_management_valid_flag
                , descriptor_info->AVC_timing_and_HRD.picture_and_timing_info_present
            )
            if( descriptor_info->AVC_timing_and_HRD.picture_and_timing_info_present )
            {
                mapi_log( LOG_LV2, "          90kHz_flag:%u\n", descriptor_info->AVC_timing_and_HRD._90kHz_flag );
                if( descriptor_info->AVC_timing_and_HRD._90kHz_flag )
                    mapi_log( LOG_LV2, "            N:%u\n"
                                       "            K:%u\n"
                                     , descriptor_info->AVC_timing_and_HRD.N
                                     , descriptor_info->AVC_timing_and_HRD.K );
                mapi_log( LOG_LV2, "          num_units_in_tick:%u\n", descriptor_info->AVC_timing_and_HRD.num_units_in_tick );
            }
            mapi_log( LOG_LV2, "        fixed_frame_rate_flag:%u\n"
                               "        temporal_poc_flag:%u\n"
                               "        picture_to_display_conversion_flag:%u\n"
                             , descriptor_info->AVC_timing_and_HRD.fixed_frame_rate_flag
                             , descriptor_info->AVC_timing_and_HRD.temporal_poc_flag
                             , descriptor_info->AVC_timing_and_HRD.picture_to_display_conversion_flag );
            break;
        PRINT_DESCRIPTOR_INFO( MPEG2_AAC_audio,
                "        MPEG2_AAC_profile:%u\n"
                "        MPEG2_AAC_channel_configuration:%u\n"
                "        MPEG2_AAC_additional_information:%u\n"
                , descriptor_info->MPEG2_AAC_audio.MPEG2_AAC_profile
                , descriptor_info->MPEG2_AAC_audio.MPEG2_AAC_channel_configuration
                , descriptor_info->MPEG2_AAC_audio.MPEG2_AAC_additional_information
            )
            break;
        PRINT_DESCRIPTOR_INFO( FlexMuxTiming,
                "        FCR_ES_ID:%u\n"
                "        FCRResolution:%u\n"
                "        FCRLength:%u\n"
                "        FmxRateLength:%u\n"
                , descriptor_info->FlexMuxTiming.FCR_ES_ID
                , descriptor_info->FlexMuxTiming.FCRResolution
                , descriptor_info->FlexMuxTiming.FCRLength
                , descriptor_info->FlexMuxTiming.FmxRateLength
            )
            break;
        PRINT_DESCRIPTOR_INFO( MPEG4_text,
                "        textFormat:%u\n"
                "        textConfigLength:%u\n"
                "        3GPPBaseFormat:%u\n"
                "        profileLevel:%u\n"
                "        durationClock:%u\n"
                "        contains_list_of_compatible_3GPPFormats_flag:%u\n"
                "        sampleDescriptionFlags:%u\n"
                "        SampleDescription_carriage_flag:%u\n"
                "        positioning_information_flag:%u\n"
                "        layer:%u\n"
                "        text_track_width:%u\n"
                "        text_track_height:%u\n"
                , descriptor_info->MPEG4_text.textFormat
                , descriptor_info->MPEG4_text.textConfigLength
                , descriptor_info->MPEG4_text._3GPPBaseFormat
                , descriptor_info->MPEG4_text.profileLevel
                , descriptor_info->MPEG4_text.durationClock
                , descriptor_info->MPEG4_text.contains_list_of_compatible_3GPPFormats_flag
                , descriptor_info->MPEG4_text.sampleDescriptionFlags
                , descriptor_info->MPEG4_text.SampleDescription_carriage_flag
                , descriptor_info->MPEG4_text.positioning_information_flag
                , descriptor_info->MPEG4_text.layer
                , descriptor_info->MPEG4_text.text_track_width
                , descriptor_info->MPEG4_text.text_track_height
            )
            if( descriptor_info->MPEG4_text.contains_list_of_compatible_3GPPFormats_flag )
            {
                mapi_log( LOG_LV2, "          number_of_formats:%u\n", descriptor_info->MPEG4_text.number_of_formats );
                if( descriptor_info->MPEG4_text.number_of_formats )
                {
                    char buf[512];
                    for( uint8_t i = 0; i < descriptor_info->MPEG4_text.number_of_formats; ++i )
                        sprintf( &(buf[i * 2]), "%02X", descriptor_info->MPEG4_text.Compatible_3GPPFormat[i] );
                    buf[descriptor_info->MPEG4_text.number_of_formats * 2] = '\0';
                    mapi_log( LOG_LV2, "          Compatible_3GPPFormat:0x%s\n", buf );
                }
            }
            if( descriptor_info->MPEG4_text.SampleDescription_carriage_flag )
            {
                mapi_log( LOG_LV2, "          number_of_SampleDescriptions:%u\n", descriptor_info->MPEG4_text.number_of_SampleDescriptions );
                for( uint8_t i = 0; i < descriptor_info->MPEG4_text.number_of_SampleDescriptions; ++i )
                {
                    mapi_log( LOG_LV2, "            Sample_index_and_description[%u]\n"
                                       "                .sample_index:%u\n"
                                       "                .displayFlags:%u\n"
                                       "                .horizontal_justification:%d\n"
                                       "                .vertical_justification:%d\n"
                                       "                .background_color_rgba:0x%02X%02X%02X%02X\n"
                                       "                .default_text_box [.top:%u, .left:%u, .bottom:%u, .right:%u]\n"
                                       "                .default_style [.startChar:%u, .endChar:%u, .font_ID:%u, .face_style_flags:%u, .font_size:%u, text_color_rgba:0x%02X%02X%02X%02X]\n"
                                       "                .font_table.entry_count:%u\n"
                                     , i
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].sample_index
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].displayFlags
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].horizontal_justification
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].vertical_justification
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].background_color_rgba[0]
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].background_color_rgba[1]
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].background_color_rgba[2]
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].background_color_rgba[3]
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].default_style.startChar
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].default_style.endChar
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].default_style.font_ID
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].default_style.face_style_flags
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].default_style.font_size
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].default_style.text_color_rgba[0]
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].default_style.text_color_rgba[1]
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].default_style.text_color_rgba[2]
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].default_style.text_color_rgba[3]
                                     , descriptor_info->MPEG4_text.Sample_index_and_description[i].font_table.entry_count );
                    for( uint8_t j = 0; j < descriptor_info->MPEG4_text.Sample_index_and_description[i].font_table.entry_count; ++j )
                        mapi_log( LOG_LV2, "                .font_table.font-entry[%u]\n"
                                           "                    .font_ID:%u\n"
                                           "                    .font_name_length:%u\n"
                                           "                    .font:%s\n"
                                         , j
                                         , descriptor_info->MPEG4_text.Sample_index_and_description[i].font_table.font_entry[j].font_ID
                                         , descriptor_info->MPEG4_text.Sample_index_and_description[i].font_table.font_entry[j].font_name_length
                                         , descriptor_info->MPEG4_text.Sample_index_and_description[i].font_table.font_entry[j].font );
                }
            }
            if( descriptor_info->MPEG4_text.positioning_information_flag )
                mapi_log( LOG_LV2, "          scene_width:%u\n"
                                   "          scene_height:%u\n"
                                   "          horizontal_scene_offset:%u\n"
                                   "          vertical_scene_offset:%u\n"
                                 , descriptor_info->MPEG4_text.scene_width
                                 , descriptor_info->MPEG4_text.scene_height
                                 , descriptor_info->MPEG4_text.horizontal_scene_offset
                                 , descriptor_info->MPEG4_text.vertical_scene_offset );
            break;
        PRINT_DESCRIPTOR_INFO( MPEG4_audio_extension,
                "        ASC_flag:%u\n"
                "        num_of_loops:%u\n"
                , descriptor_info->MPEG4_audio_extension.ASC_flag
                , descriptor_info->MPEG4_audio_extension.num_of_loops
            )
            if( descriptor_info->MPEG4_audio_extension.num_of_loops )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->MPEG4_audio_extension.num_of_loops; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->MPEG4_audio_extension.audioProfileLevelIndication[i] );
                buf[descriptor_info->MPEG4_audio_extension.num_of_loops * 2] = '\0';
                mapi_log( LOG_LV2, "          audioProfileLevelIndication:0x%s\n", buf );
            }
            if( descriptor_info->MPEG4_audio_extension.ASC_flag )
            {
                mapi_log( LOG_LV2, "          ASC_size:%u\n", descriptor_info->MPEG4_audio_extension.ASC_size );
                if( descriptor_info->MPEG4_audio_extension.ASC_size )
                {
                    char buf[512];
                    for( uint8_t i = 0; i < descriptor_info->MPEG4_audio_extension.ASC_size; ++i )
                        sprintf( &(buf[i * 2]), "%02X", descriptor_info->MPEG4_audio_extension.audioSpecificConfig[i] );
                    buf[descriptor_info->MPEG4_audio_extension.ASC_size * 2] = '\0';
                    mapi_log( LOG_LV2, "          audioSpecificConfig:0x%s\n", buf );
                }
            }
            break;
        PRINT_DESCRIPTOR_INFO( Auxiliary_video_stream,
                "        aux_video_codedstreamtype:%u\n"
             // "        si_rbsp_length:%u\n"
                , descriptor_info->Auxiliary_video_stream.aux_video_codedstreamtype
             // , descriptor_info->Auxiliary_video_stream.si_rbsp_length
            )
#ifdef DEBUG
            if( descriptor_info->Auxiliary_video_stream.si_rbsp_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->Auxiliary_video_stream.si_rbsp_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->Auxiliary_video_stream.si_rbsp[i] );
                buf[descriptor_info->Auxiliary_video_stream.si_rbsp_length * 2] = '\0';
                mapi_log( LOG_LV2, "          si_rbsp:0x%s\n", buf );
            }
#endif
            break;
        PRINT_DESCRIPTOR_INFO( SVC_extension,
                "        width:%u\n"
                "        height:%u\n"
                "        frame_rate:%u\n"
                "        average_bitrate:%u\n"
                "        maximum_bitrate:%u\n"
                "        dependency_id:%u\n"
                "        quality_id_start:%u\n"
                "        quality_id_end:%u\n"
                "        temporal_id_start:%u\n"
                "        temporal_id_end:%u\n"
                "        no_sei_nal_unit_present:%u\n"
                , descriptor_info->SVC_extension.width
                , descriptor_info->SVC_extension.height
                , descriptor_info->SVC_extension.frame_rate
                , descriptor_info->SVC_extension.average_bitrate
                , descriptor_info->SVC_extension.maximum_bitrate
                , descriptor_info->SVC_extension.dependency_id
                , descriptor_info->SVC_extension.quality_id_start
                , descriptor_info->SVC_extension.quality_id_end
                , descriptor_info->SVC_extension.temporal_id_start
                , descriptor_info->SVC_extension.temporal_id_end
                , descriptor_info->SVC_extension.no_sei_nal_unit_present
            )
            break;
        PRINT_DESCRIPTOR_INFO( MVC_extension,
                "        average_bitrate:%u\n"
                "        maximum_bitrate:%u\n"
                "        view_association_not_present:%u\n"
                "        base_view_is_left_eyeview:%u\n"
                "        view_order_index_min:%u\n"
                "        view_order_index_max:%u\n"
                "        temporal_id_start:%u\n"
                "        temporal_id_end:%u\n"
                "        no_sei_nal_unit_present:%u\n"
                "        no_prefix_nal_unit_present:%u\n"
                , descriptor_info->MVC_extension.average_bitrate
                , descriptor_info->MVC_extension.maximum_bitrate
                , descriptor_info->MVC_extension.view_association_not_present
                , descriptor_info->MVC_extension.base_view_is_left_eyeview
                , descriptor_info->MVC_extension.view_order_index_min
                , descriptor_info->MVC_extension.view_order_index_max
                , descriptor_info->MVC_extension.temporal_id_start
                , descriptor_info->MVC_extension.temporal_id_end
                , descriptor_info->MVC_extension.no_sei_nal_unit_present
                , descriptor_info->MVC_extension.no_prefix_nal_unit_present
            )
            break;
        PRINT_DESCRIPTOR_INFO( J2K_video,
                "        profile_and_level:%u\n"
                "        horizontal_size:%u\n"
                "        vertical_size:%u\n"
                "        max_bit_rate:%u\n"
                "        max_buffer_size:%u\n"
                "        DEN_frame_rate:%u\n"
                "        NUM_frame_rate:%u\n"
                "        color_specification:%u\n"
                "        still_mode:%u\n"
                "        interlaced_video:%u\n"
                , descriptor_info->J2K_video.profile_and_level
                , descriptor_info->J2K_video.horizontal_size
                , descriptor_info->J2K_video.vertical_size
                , descriptor_info->J2K_video.max_bit_rate
                , descriptor_info->J2K_video.max_buffer_size
                , descriptor_info->J2K_video.DEN_frame_rate
                , descriptor_info->J2K_video.NUM_frame_rate
                , descriptor_info->J2K_video.color_specification
                , descriptor_info->J2K_video.still_mode
                , descriptor_info->J2K_video.interlaced_video
            )
            /* private_data_byte */
#ifdef DEBUG
            if( descriptor_info->J2K_video.private_data_byte_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->J2K_video.private_data_byte_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->J2K_video.private_data_byte[i] );
                buf[descriptor_info->J2K_video.private_data_byte_length * 2] = '\0';
                mapi_log( LOG_LV2, "          private_data_byte:0x%s\n", buf );
            }
#endif
            break;
        PRINT_DESCRIPTOR_INFO( MVC_operation_point,
                "        profile_idc:%u\n"
                "        constraint_set0_flag:%u\n"
                "        constraint_set1_flag:%u\n"
                "        constraint_set2_flag:%u\n"
                "        constraint_set3_flag:%u\n"
                "        constraint_set4_flag:%u\n"
                "        constraint_set5_flag:%u\n"
                "        AVC_compatible_flags:%u\n"
                "        level_count:%u\n"
                , descriptor_info->MVC_operation_point.profile_idc
                , descriptor_info->MVC_operation_point.constraint_set0_flag
                , descriptor_info->MVC_operation_point.constraint_set1_flag
                , descriptor_info->MVC_operation_point.constraint_set2_flag
                , descriptor_info->MVC_operation_point.constraint_set3_flag
                , descriptor_info->MVC_operation_point.constraint_set4_flag
                , descriptor_info->MVC_operation_point.constraint_set5_flag
                , descriptor_info->MVC_operation_point.AVC_compatible_flags
                , descriptor_info->MVC_operation_point.level_count
            )
            for( uint8_t i = 0; i < descriptor_info->MVC_operation_point.level_count; ++i )
            {
                mapi_log( LOG_LV2, "          levels[%u]\n"
                                   "              .level_idc:%u\n"
                                   "              .operation_points_count:%u\n"
                                 , i
                                 , descriptor_info->MVC_operation_point.levels[i].level_idc
                                 , descriptor_info->MVC_operation_point.levels[i].operation_points_count );
                for( uint8_t j = 0; j < descriptor_info->MVC_operation_point.levels[i].operation_points_count; ++j )
                {
                    mapi_log( LOG_LV2, "              .operation_points[%u]\n"
                                       "                  .applicable_temporal_id:%u\n"
                                       "                  .num_target_output_views:%u\n"
                                       "                  .ES_count:%u\n"
                                     , j
                                     , descriptor_info->MVC_operation_point.levels[i].operation_points[j].applicable_temporal_id
                                     , descriptor_info->MVC_operation_point.levels[i].operation_points[j].num_target_output_views
                                     , descriptor_info->MVC_operation_point.levels[i].operation_points[j].ES_count );
                    for( uint8_t k = 0; k < descriptor_info->MVC_operation_point.levels[i].operation_points[j].ES_count; ++k )
                        mapi_log( LOG_LV2, "                  .ES_reference[%u]:%u\n"
                                         , k
                                         , descriptor_info->MVC_operation_point.levels[i].operation_points[j].ES_reference[k] );
                }
            }
            break;
        PRINT_DESCRIPTOR_INFO( MPEG2_stereoscopic_video_format,
                "        stereo_video_arrangement_type_present:%u\n"
                , descriptor_info->MPEG2_stereoscopic_video_format.stereo_video_arrangement_type_present
            )
            if( descriptor_info->MPEG2_stereoscopic_video_format.stereo_video_arrangement_type_present )
                mapi_log( LOG_LV2, "          arrangement_type:%u\n", descriptor_info->MPEG2_stereoscopic_video_format.arrangement_type );
            break;
        PRINT_DESCRIPTOR_INFO( Stereoscopic_program_info,
                "        stereoscopic_service_type:%u\n"
                , descriptor_info->Stereoscopic_program_info.stereoscopic_service_type
            )
            break;
        PRINT_DESCRIPTOR_INFO( Stereoscopic_video_info,
                "        base_video_flag:%u\n"
                , descriptor_info->Stereoscopic_video_info.base_video_flag
            )
            if( descriptor_info->Stereoscopic_video_info.base_video_flag )
                mapi_log( LOG_LV2, "          arrangement_type:%u\n", descriptor_info->Stereoscopic_video_info.leftview_flag );
            else
                mapi_log( LOG_LV2, "          usable_as_2D:%u\n"
                                   "          horizontal_upsampling_factor:%u\n"
                                   "          vertical_upsampling_factor:%u\n"
                                 , descriptor_info->Stereoscopic_video_info.usable_as_2D
                                 , descriptor_info->Stereoscopic_video_info.horizontal_upsampling_factor
                                 , descriptor_info->Stereoscopic_video_info.vertical_upsampling_factor );
            break;
        PRINT_DESCRIPTOR_INFO( Transport_profile,
                "        transport_profile:%u\n"
                , descriptor_info->Transport_profile.transport_profile
            )
#ifdef DEBUG
            if( descriptor_info->Transport_profile.private_data_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->Transport_profile.private_data_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->Transport_profile.private_data[i] );
                buf[descriptor_info->Transport_profile.private_data_length * 2] = '\0';
                mapi_log( LOG_LV2, "          private_data:0x%s\n", buf );
            }
#endif
            break;
        PRINT_DESCRIPTOR_INFO( HEVC_video,
                "        profile_space:%u\n"
                "        tier_flag:%u\n"
                "        profile_idc:%u\n"
                "        profile_compatibility_indication:%u\n"
                "        progressive_source_flag:%u\n"
                "        interlaced_source_flag:%u\n"
                "        non_packed_constraint_flag:%u\n"
                "        frame_only_constraint_flag:%u\n"
                "        copied_44bits:%" PRIu64 "\n"
                "        level_idc:%u\n"
                "        temporal_layer_subset_flag:%u\n"
                "        HEVC_still_present_flag:%u\n"
                "        HEVC_24hr_picture_present_flag:%u\n"
                "        sub_pic_hrd_params_not_present_flag:%u\n"
                "        HDR_WCG_idc:%u\n"
                , descriptor_info->HEVC_video.profile_space
                , descriptor_info->HEVC_video.tier_flag
                , descriptor_info->HEVC_video.profile_idc
                , descriptor_info->HEVC_video.profile_compatibility_indication
                , descriptor_info->HEVC_video.progressive_source_flag
                , descriptor_info->HEVC_video.interlaced_source_flag
                , descriptor_info->HEVC_video.non_packed_constraint_flag
                , descriptor_info->HEVC_video.frame_only_constraint_flag
                , descriptor_info->HEVC_video.copied_44bits
                , descriptor_info->HEVC_video.level_idc
                , descriptor_info->HEVC_video.temporal_layer_subset_flag
                , descriptor_info->HEVC_video.HEVC_still_present_flag
                , descriptor_info->HEVC_video.HEVC_24hr_picture_present_flag
                , descriptor_info->HEVC_video.sub_pic_hrd_params_not_present_flag
                , descriptor_info->HEVC_video.HDR_WCG_idc
            )
            if( descriptor_info->HEVC_video.temporal_layer_subset_flag )
                mapi_log( LOG_LV2, "          temporal_id_min:%u\n"
                                   "          temporal_id_max:%u\n"
                                 , descriptor_info->HEVC_video.temporal_id_min
                                 , descriptor_info->HEVC_video.temporal_id_max );
            break;
        PRINT_DESCRIPTOR_INFO( Extension,
                "        extension_descriptor_tag:%u\n"
                , descriptor_info->Extension.extension_descriptor_tag
            )
            /* extension_descriptor_data */         // FIXME
#ifdef DEBUG
            if( descriptor_info->Extension.extension_descriptor_length )
            {
                char buf[512];
                for( uint8_t i = 0; i < descriptor_info->Extension.extension_descriptor_length; ++i )
                    sprintf( &(buf[i * 2]), "%02X", descriptor_info->Extension.extension_descriptor_data[i] );
                buf[descriptor_info->Extension.extension_descriptor_length * 2] = '\0';
                mapi_log( LOG_LV2, "          extension_descriptor_data:0x%s\n", buf );
            }
#endif
            break;
		/*  */
        PRINT_DESCRIPTOR_INFO( component,
                "        component_tag:0x%02X\n"
                , descriptor_info->component.component_tag
            )
            break;
        PRINT_DESCRIPTOR_INFO( stream_identifier,
                "        component_tag:0x%02X\n"
                , descriptor_info->stream_identifier.component_tag
            )
            break;
        PRINT_DESCRIPTOR_INFO( CA_identifier,
                "        CA_system_id:0x%04X\n"
                , descriptor_info->CA_identifier.CA_system_id
            )
            break;
        default :
            break;
    }
}
#undef PRINT_DESCRIPTOR_INFO

#define FI_U32( a, b, c, d )    ( (uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)c << 8 | (uint32_t)d )
#define FI_CHECK( tag, type )   \
{                               \
    case tag :                  \
        return type;            \
}

extern mpeg_stream_type mpeg_stream_get_registration_stream_type( mpeg_descriptor_info_t *descriptor_info )
{
    registration_descriptor_info_t *registration = &(descriptor_info->registration);
    switch( registration->format_identifier )
    {
        FI_CHECK( FI_U32( 'A', 'C', '-', '3' ), STREAM_AUDIO_AC3 )
        FI_CHECK( FI_U32( 'D', 'T', 'S', '1' ), STREAM_AUDIO_DTS )
        FI_CHECK( FI_U32( 'D', 'T', 'S', '2' ), STREAM_AUDIO_DTS )
        FI_CHECK( FI_U32( 'D', 'T', 'S', '3' ), STREAM_AUDIO_DTS )
        FI_CHECK( FI_U32( 'E', 'A', 'C', '3' ), STREAM_AUDIO_EAC3 )
        FI_CHECK( FI_U32( 'm', 'l', 'p', 'a' ), STREAM_AUDIO_MLP )
        FI_CHECK( FI_U32( 'H', 'E', 'V', 'C' ), STREAM_VIDEO_HEVC )
        FI_CHECK( FI_U32( 'V', 'C', '-', '1' ), STREAM_VIDEO_VC1 )
        FI_CHECK( FI_U32( 'D', 'V', 'D', 'F' ), STREAM_AUDIO_LPCM )         // FIXME
        case FI_U32( 'H', 'D', 'M', 'V' ) :
            if( registration->additional_identification_info_length >= 4
             && registration->additional_identification_info[0] == 0xFF )
                return registration->additional_identification_info[1];     // FIXME
            break;
    }
    return STREAM_INVALID;
}

static mpeg_stream_group_type judge_group_type_from_registration_descriptor( mpeg_descriptor_info_t *descriptor_info )
{
    registration_descriptor_info_t *registration = &(descriptor_info->registration);
    switch( registration->format_identifier )
    {
        FI_CHECK( FI_U32( 'A', 'C', '-', '3' ), STREAM_IS_DOLBY_AUDIO )
        FI_CHECK( FI_U32( 'D', 'T', 'S', '1' ), STREAM_IS_DTS_AUDIO )
        FI_CHECK( FI_U32( 'D', 'T', 'S', '2' ), STREAM_IS_DTS_AUDIO )
        FI_CHECK( FI_U32( 'D', 'T', 'S', '3' ), STREAM_IS_DTS_AUDIO )
        FI_CHECK( FI_U32( 'E', 'A', 'C', '3' ), STREAM_IS_DOLBY_AUDIO )
        FI_CHECK( FI_U32( 'm', 'l', 'p', 'a' ), STREAM_IS_AUDIO )
        FI_CHECK( FI_U32( 'H', 'E', 'V', 'C' ), STREAM_IS_VIDEO )
        FI_CHECK( FI_U32( 'V', 'C', '-', '1' ), STREAM_IS_EXTENDED_VIDEO )
        FI_CHECK( FI_U32( 'D', 'V', 'D', 'F' ), STREAM_IS_PCM_AUDIO )       // FIXME
        case FI_U32( 'H', 'D', 'M', 'V' ) :
            if( registration->additional_identification_info_length >= 4
             && registration->additional_identification_info[0] == 0xFF )
            {
                /* audio: 0xFF-XX-31-7F, video: 0xFF-XX-44-3F */            // FIXME
                mpeg_stream_type stream_type = registration->additional_identification_info[1];
                if( stream_type != STREAM_PES_PRIVATE_DATA
                 && stream_type != STREAM_VIDEO_PRIVATE /* = STREAM_AUDIO_LPCM */ )
                    return mpeg_stream_judge_type( stream_type, 0, NULL );
                if( registration->additional_identification_info[2] == 0x31
                 && registration->additional_identification_info[3] == 0x7F )
                    return STREAM_IS_PCM_AUDIO;
                if( registration->additional_identification_info[2] == 0x44
                 && registration->additional_identification_info[3] == 0x3F )
                    return STREAM_IS_PRIVATE_VIDEO;
            }
            break;
    }
    return STREAM_IS_UNKNOWN;
}

#undef FI_U32
#undef FI_CHECK

extern mpeg_stream_group_type mpeg_stream_judge_type
(
    mpeg_stream_type            stream_type,
    uint16_t                    descriptor_num,
    mpeg_descriptor_info_t     *descriptor_info
)
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
            stream_judge = STREAM_IS_PRIVATE_VIDEO;
            for( uint16_t i = 0; i < descriptor_num; ++i )
            {
                if( descriptor_info->tags[i] == registration_descriptor )
                    stream_judge = judge_group_type_from_registration_descriptor( descriptor_info );
                if( stream_judge != STREAM_IS_UNKNOWN )
                    break;
            }
            break;
        case STREAM_AUDIO_AC3 :
        case STREAM_AUDIO_EAC3 :
        case STREAM_AUDIO_DDPLUS :
        case STREAM_AUDIO_DDPLUS_SUB :
            stream_judge = STREAM_IS_DOLBY_AUDIO;
            break;
        case STREAM_AUDIO_MLP :
            stream_judge = STREAM_IS_AUDIO;     // FIXME
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
                if( descriptor_info->tags[i] == registration_descriptor )
                    stream_judge = judge_group_type_from_registration_descriptor( descriptor_info );
                else if( descriptor_info->tags[i] == stream_identifier_descriptor )
                {
                    uint8_t component_tag = descriptor_info->stream_identifier.component_tag;
                    if( component_tag == 0x30 )
                        stream_judge = STREAM_IS_ARIB_CAPTION;
                    else if( component_tag == 0x38 )
                        stream_judge = STREAM_IS_ARIB_STRING_SUPER;
                }
                if( stream_judge != STREAM_IS_UNKNOWN )
                    break;
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
