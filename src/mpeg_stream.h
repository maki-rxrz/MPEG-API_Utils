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
    PES_STEAM_ID_PROGRAM_STERAM_MAP      ,
    PES_STEAM_ID_PRIVATE_STREAM_1        ,
    PES_STEAM_ID_PADDING_STREAM          ,
    PES_STEAM_ID_PRIVATE_STREAM_2        ,
    PES_STEAM_ID_AUDIO_STREAM            ,
    PES_STEAM_ID_VIDEO_STREAM            ,
    PES_STEAM_ID_ECM_STREAM              ,
    PES_STEAM_ID_EMM_STREAM              ,
    PES_STEAM_ID_DSMCC_STREAM            ,
    PES_STEAM_ID_ISO_IEC_13522_STREAM    ,
    PES_STEAM_ID_ITU_T_H222_1_A          ,
    PES_STEAM_ID_ITU_T_H222_1_B          ,
    PES_STEAM_ID_ITU_T_H222_1_C          ,
    PES_STEAM_ID_ITU_T_H222_1_D          ,
    PES_STEAM_ID_ITU_T_H222_1_E          ,
    PES_STEAM_ID_ANCILLARY_STREAM        ,
    PES_STEAM_ID_SL_PACKETIZED_STREAM    ,
    PES_STEAM_ID_FLEXMUX_STREAM          ,
    PES_STEAM_ID_METADATA_STREAM         ,
    PES_STEAM_ID_EXTENDED_STREAM_ID      ,
    PES_STEAM_ID_RESERVED_DATA_STREAM    ,
    PES_STEAM_ID_PROGRAM_STREAM_DIRECTORY,
    PES_STEAM_ID_TYPE_MAX                ,
    PES_STEAM_ID_INVALID = PES_STEAM_ID_TYPE_MAX
} mpeg_pes_stream_id_type;

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
    video_stream_descriptor                    = 0x02,
    audio_stream_descriptor                    = 0x03,
    hierarchy_descriptor                       = 0x04,
    registration_descriptor                    = 0x05,
    data_stream_alignment_descriptor           = 0x06,
    target_background_grid_descriptor          = 0x07,
    video_window_descriptor                    = 0x08,
    conditional_access_descriptor              = 0x09,
    ISO_639_language_descriptor                = 0x0A,
    system_clock_descriptor                    = 0x0B,
    multiplex_buffer_utilization_descriptor    = 0x0C,
    copyright_descriptor                       = 0x0D,
    maximum_bitrate_descriptor                 = 0x0E,
    private_data_indicator_descriptor          = 0x0F,
    smoothing_buffer_descriptor                = 0x10,
    STD_descriptor                             = 0x11,
    ibp_descriptor                             = 0x12,
    /* 19-26  : Defined in ISO/IEC 13818-6                      */
    MPEG4_video_descriptor                     = 0x1B,
    MPEG4_audio_descriptor                     = 0x1C,
    IOD_descriptor                             = 0x1D,
    SL_descriptor                              = 0x1E,
    FMC_descriptor                             = 0x1F,
    External_ES_ID_descriptor                  = 0x20,
    MuxCode_descriptor                         = 0x21,
    FmxBufferSize_descriptor                   = 0x22,
    MultiplexBuffer_descriptor                 = 0x23,
    content_labeling_descriptor                = 0x24,
    metadata_pointer_descriptor                = 0x25,
    metadata_descriptor                        = 0x26,
    metadata_STD_descriptor                    = 0x27,
    AVC_video_descriptor                       = 0x28,
    IPMP_descriptor                            = 0x29,      /* defined in ISO/IEC 13818-11, MPEG-2 IPMP */
    AVC_timing_and_HRD_descriptor              = 0x2A,
    MPEG2_AAC_audio_descriptor                 = 0x2B,
    FlexMuxTiming_descriptor                   = 0x2C,
    MPEG4_text_descriptor                      = 0x2D,
    MPEG4_audio_extension_descriptor           = 0x2E,
    Auxiliary_video_stream_descriptor          = 0x2F,
    SVC_extension_descriptor                   = 0x30,
    MVC_extension_descriptor                   = 0x31,
    J2K_video_descriptor                       = 0x32,
    MVC_operation_point_descriptor             = 0x33,
    MPEG2_stereoscopic_video_format_descriptor = 0x34,
    Stereoscopic_program_info_descriptor       = 0x35,
    Stereoscopic_video_info_descriptor         = 0x36,
    Transport_profile_descriptor               = 0x37,
    HEVC_video_descriptor                      = 0x38,
    /* 57-62  : Rec. ITU-T H.222.0 | ISO/IEC 13818-1 Reserved   */
    Extension_descriptor                       = 0x3F,
    /* 64-255 : User Private                                    */
    component_descriptor                       = 0x50,
    stream_identifier_descriptor               = 0x52,
    CA_identifier_descriptor                   = 0x53,
    DESCRIPTOR_TAG_MAX_VALUE                   = 0xFF
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
    uint8_t         additional_identification_info_length;
    uint8_t         additional_identification_info[251];
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
    uint8_t         private_data_byte_length;
    uint8_t         private_data_byte[251];
} DESCRIPTOR_INFO( conditional_access );

typedef struct {
    uint8_t         data_num;
    struct {
        uint32_t    ISO_639_language_code;
        uint8_t     audio_type;
    } data[64];
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
    uint32_t        copyright_identifier;
    uint8_t         additional_copyright_info_length;
    uint8_t         additional_copyright_info[251];
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
 // InitialObjectDescriptor();  /* defined in 8.6.3.1 of ISO/IEC 14496-1. */
    uint8_t         InitialObjectDescriptor_length;     // FIXME
    uint8_t         InitialObjectDescriptor_data[253];
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
 // MuxCodeTableEntry();        /* defined in 11.2.4.3 of ISO/IEC 14496-1. */
    uint8_t         length;
    uint8_t         MuxCode;
    uint8_t         version;
    uint8_t         substructureCount;
    struct {
        uint8_t     slotCount;
        uint8_t     repetitionCount;
        struct {
            uint8_t flexMuxChannel;
            uint8_t numberOfBytes;
        } slots[32];
    } subs[255];
} DESCRIPTOR_INFO( MuxCode );

typedef struct {
 // DefaultFlexMuxBufferDescriptor()    /* defined in 11.2 of ISO/IEC 14496-1. */
 // FlexMuxBufferDescriptor()           /* defined in 11.2 of ISO/IEC 14496-1. */
    uint8_t         descriptor_length;      // FIXME
    uint8_t         descriptor_data[255];
} DESCRIPTOR_INFO( FmxBufferSize );

typedef struct {
    uint32_t        MB_buffer_size;
    uint32_t        TB_leak_rate;
} DESCRIPTOR_INFO( MultiplexBuffer );

typedef struct {
    uint16_t        metadata_application_format;
    uint32_t        metadata_application_format_identifier;
    uint8_t         content_reference_id_record_flag;
    uint8_t         content_time_base_indicator;
    uint8_t         content_reference_id_record_length;
    uint8_t         content_reference_id_byte[255];
    uint64_t        content_time_base_value;
    uint64_t        metadata_time_base_value;
    uint8_t         contentId;
    uint8_t         time_base_association_data_length;
    uint8_t         time_base_association_data[255];
    uint8_t         private_data_byte_length;
    uint8_t         private_data_byte[255];
} DESCRIPTOR_INFO( content_labeling );

typedef struct {
    uint16_t        metadata_application_format;
    uint32_t        metadata_application_format_identifier;
    uint8_t         metadata_format;
    uint32_t        metadata_format_identifier;
    uint8_t         metadata_service_id;
    uint8_t         metadata_locator_record_flag;
    uint8_t         MPEG_carriage_flags;
    uint8_t         metadata_locator_record_length;
    uint8_t         metadata_locator_record_byte[255];
    uint16_t        program_number;
    uint16_t        transport_stream_location;
    uint16_t        ransport_stream_id;
    uint8_t         private_data_byte_length;
    uint8_t         private_data_byte[255];
} DESCRIPTOR_INFO( metadata_pointer );

typedef struct {
    uint16_t        metadata_application_format;
    uint32_t        metadata_application_format_identifier;
    uint8_t         metadata_format;
    uint32_t        metadata_format_identifier;
    uint8_t         metadata_service_id;
    uint8_t         decoder_config_flags;
    uint8_t         DSM_CC_flag;
    uint8_t         service_identification_length;
    uint8_t         service_identification_record_byte[255];
    uint8_t         decoder_config_length;
    uint8_t         decoder_config_byte[255];
    uint8_t         dec_config_identification_record_length;
    uint8_t         dec_config_identification_record_byte[255];
    uint8_t         decoder_config_metadata_service_id;
    uint8_t         reserved_data_length;
    uint8_t         reserved_data[255];
    uint8_t         private_data_byte_length;
    uint8_t         private_data_byte[255];
} DESCRIPTOR_INFO( metadata );

typedef struct {
    uint32_t        metadata_input_leak_rate;
    uint32_t        metadata_buffer_size;
    uint32_t        metadata_output_leak_rate;
} DESCRIPTOR_INFO( metadata_STD );

typedef struct {
    uint8_t         profile_idc;
    uint8_t         constraint_set0_flag;
    uint8_t         constraint_set1_flag;
    uint8_t         constraint_set2_flag;
    uint8_t         constraint_set3_flag;
    uint8_t         constraint_set4_flag;
    uint8_t         constraint_set5_flag;
    uint8_t         AVC_compatible_flags;
    uint8_t         level_idc;
    uint8_t         AVC_still_present;
    uint8_t         AVC_24_hour_picture_flag;
    uint8_t         Frame_Packing_SEI_not_present_flag;
} DESCRIPTOR_INFO( AVC_video );

typedef struct {
    /* defined in ISO/IEC 13818-11, MPEG-2 IPMP */
    uint8_t         descriptor_length;      // FIXME
    uint8_t         descriptor_data[255];
} DESCRIPTOR_INFO( IPMP );

typedef struct {
    uint8_t         hrd_management_valid_flag;
    uint8_t         picture_and_timing_info_present;
    uint8_t         _90kHz_flag;
    uint32_t        N;
    uint32_t        K;
    uint32_t        num_units_in_tick;
    uint8_t         fixed_frame_rate_flag;
    uint8_t         temporal_poc_flag;
    uint8_t         picture_to_display_conversion_flag;
} DESCRIPTOR_INFO( AVC_timing_and_HRD );

typedef struct {
    uint8_t         MPEG2_AAC_profile;
    uint8_t         MPEG2_AAC_channel_configuration;
    uint8_t         MPEG2_AAC_additional_information;
} DESCRIPTOR_INFO( MPEG2_AAC_audio );

typedef struct {
    uint16_t        FCR_ES_ID;
    uint32_t        FCRResolution;
    uint8_t         FCRLength;
    uint8_t         FmxRateLength;
} DESCRIPTOR_INFO( FlexMuxTiming );

typedef struct {
 // TextConfig();               /* defined in ISO/IEC 14496-17. */
    uint8_t         textFormat;
    uint16_t        textConfigLength;
 // formatSpecificTextConfig();
    uint8_t         _3GPPBaseFormat;
    uint8_t         profileLevel;
    uint32_t        durationClock;
    uint8_t         contains_list_of_compatible_3GPPFormats_flag;
    uint8_t         sampleDescriptionFlags;
    uint8_t         SampleDescription_carriage_flag;
    uint8_t         positioning_information_flag;
    uint8_t         layer;
    uint16_t        text_track_width;
    uint16_t        text_track_height;
    uint8_t         number_of_formats;
    uint8_t         Compatible_3GPPFormat[255];
    uint8_t         number_of_SampleDescriptions;
 // Sample_index_and_description()(number-of-SampleDescriptions);
    struct {
        uint8_t         sample_index;
     // SampleDescription();    /* specified in 3GPP TS 26.245. */
        uint32_t        displayFlags;
        int8_t          horizontal_justification;
        int8_t          vertical_justification;
        uint8_t         background_color_rgba[4];
     // BoxRecord       default-text-box;
        struct {
            int16_t    top;
            int16_t    left;
            int16_t    bottom;
            int16_t    right;
        } default_text_box;
     // StyleRecord     default-style;
        struct {
            uint16_t    startChar;
            uint16_t    endChar;
            uint16_t    font_ID;
            uint8_t     face_style_flags;
            uint8_t     font_size;
            uint8_t     text_color_rgba[4];
        } default_style;
     // FontTableBox    font-table;
        struct {
            uint16_t    entry_count;
         // FontRecord    font-entry[entry-count];
            struct {
                uint16_t    font_ID;
                uint8_t     font_name_length;
                uint8_t     font[255];
            } font_entry[255];
        } font_table;
    } Sample_index_and_description[255];
    uint16_t        scene_width;
    uint16_t        scene_height;
    uint16_t        horizontal_scene_offset;
    uint16_t        vertical_scene_offset;
} DESCRIPTOR_INFO( MPEG4_text );

typedef struct {
    uint8_t         ASC_flag;
    uint8_t         num_of_loops;
    uint8_t         audioProfileLevelIndication[16];
    uint8_t         ASC_size;
 // audioSpecificConfig();      /* specified in 1.6.2.1 in ISO/IEC 14496-3. */
    uint8_t         audioSpecificConfig[255];       // FIXME
} DESCRIPTOR_INFO( MPEG4_audio_extension );

typedef struct {
    uint8_t         aux_video_codedstreamtype;
 // si_rbsp(descriptor_length-1);
    uint8_t         si_rbsp_length;
    uint8_t         si_rbsp[254];
} DESCRIPTOR_INFO( Auxiliary_video_stream );

typedef struct {
    uint16_t        width;
    uint16_t        height;
    uint16_t        frame_rate;
    uint16_t        average_bitrate;
    uint16_t        maximum_bitrate;
    uint8_t         dependency_id;
    uint8_t         quality_id_start;
    uint8_t         quality_id_end;
    uint8_t         temporal_id_start;
    uint8_t         temporal_id_end;
    uint8_t         no_sei_nal_unit_present;
} DESCRIPTOR_INFO( SVC_extension );

typedef struct {
    uint16_t        average_bitrate;
    uint16_t        maximum_bitrate;
    uint8_t         view_association_not_present;
    uint8_t         base_view_is_left_eyeview;
    uint16_t        view_order_index_min;
    uint16_t        view_order_index_max;
    uint8_t         temporal_id_start;
    uint8_t         temporal_id_end;
    uint8_t         no_sei_nal_unit_present;
    uint8_t         no_prefix_nal_unit_present;
} DESCRIPTOR_INFO( MVC_extension );

typedef struct {
    uint16_t        profile_and_level;
    uint32_t        horizontal_size;
    uint32_t        vertical_size;
    uint32_t        max_bit_rate;
    uint32_t        max_buffer_size;
    uint16_t        DEN_frame_rate;
    uint16_t        NUM_frame_rate;
    uint8_t         color_specification;
    uint8_t         still_mode;
    uint8_t         interlaced_video;
    uint8_t         private_data_byte_length;
    uint8_t         private_data_byte[231];
} DESCRIPTOR_INFO( J2K_video );

typedef struct {
    uint8_t         profile_idc;
    uint8_t         constraint_set0_flag;
    uint8_t         constraint_set1_flag;
    uint8_t         constraint_set2_flag;
    uint8_t         constraint_set3_flag;
    uint8_t         constraint_set4_flag;
    uint8_t         constraint_set5_flag;
    uint8_t         AVC_compatible_flags;
    uint8_t         level_count;
    struct {
        uint8_t     level_idc;
        uint8_t     operation_points_count;
        struct {
            uint8_t     applicable_temporal_id;
            uint8_t     num_target_output_views;
            uint8_t     ES_count;
            uint8_t     ES_reference[255];
        } operation_points[255];
    } levels[255];
} DESCRIPTOR_INFO( MVC_operation_point );       // FIXME

typedef struct {
    uint8_t         stereo_video_arrangement_type_present;
    uint8_t         arrangement_type;
} DESCRIPTOR_INFO( MPEG2_stereoscopic_video_format );

typedef struct {
    uint8_t         stereoscopic_service_type;
} DESCRIPTOR_INFO( Stereoscopic_program_info );

typedef struct {
    uint8_t         base_video_flag;
    uint8_t         leftview_flag;
    uint8_t         usable_as_2D;
    uint8_t         horizontal_upsampling_factor;
    uint8_t         vertical_upsampling_factor;
} DESCRIPTOR_INFO( Stereoscopic_video_info );

typedef struct {
    uint8_t         transport_profile;
    uint8_t         private_data_length;
    uint8_t         private_data[254];
} DESCRIPTOR_INFO( Transport_profile );

typedef struct {
    uint8_t         profile_space;
    uint8_t         tier_flag;
    uint8_t         profile_idc;
    uint32_t        profile_compatibility_indication;
    uint8_t         progressive_source_flag;
    uint8_t         interlaced_source_flag;
    uint8_t         non_packed_constraint_flag;
    uint8_t         frame_only_constraint_flag;
    uint64_t        copied_44bits;
    uint8_t         level_idc;
    uint8_t         temporal_layer_subset_flag;
    uint8_t         HEVC_still_present_flag;
    uint8_t         HEVC_24hr_picture_present_flag;
    uint8_t         sub_pic_hrd_params_not_present_flag;
    uint8_t         HDR_WCG_idc;
    uint8_t         temporal_id_min;
    uint8_t         temporal_id_max;
} DESCRIPTOR_INFO( HEVC_video );

typedef struct {
    uint8_t         extension_descriptor_tag;
 // XXXX_extension_descriptor();
    uint8_t         extension_descriptor_length;        // FIXME
    uint8_t         extension_descriptor_data[254];
} DESCRIPTOR_INFO( Extension );

/*  */
typedef struct {
    uint8_t         component_tag;
} DESCRIPTOR_INFO( component );

typedef struct {
    uint8_t         component_tag;
} DESCRIPTOR_INFO( stream_identifier );

typedef struct {
    uint16_t        CA_system_id;
} DESCRIPTOR_INFO( CA_identifier );

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
    STRUCT_DESCRIPTOR( content_labeling )
    STRUCT_DESCRIPTOR( metadata_pointer )
    STRUCT_DESCRIPTOR( metadata )
    STRUCT_DESCRIPTOR( metadata_STD )
    STRUCT_DESCRIPTOR( AVC_video )
    STRUCT_DESCRIPTOR( IPMP )
    STRUCT_DESCRIPTOR( AVC_timing_and_HRD )
    STRUCT_DESCRIPTOR( MPEG2_AAC_audio )
    STRUCT_DESCRIPTOR( FlexMuxTiming )
    STRUCT_DESCRIPTOR( MPEG4_text )
    STRUCT_DESCRIPTOR( MPEG4_audio_extension )
    STRUCT_DESCRIPTOR( Auxiliary_video_stream )
    STRUCT_DESCRIPTOR( SVC_extension )
    STRUCT_DESCRIPTOR( MVC_extension )
    STRUCT_DESCRIPTOR( J2K_video )
    STRUCT_DESCRIPTOR( MVC_operation_point )
    STRUCT_DESCRIPTOR( MPEG2_stereoscopic_video_format )
    STRUCT_DESCRIPTOR( Stereoscopic_program_info )
    STRUCT_DESCRIPTOR( Stereoscopic_video_info )
    STRUCT_DESCRIPTOR( Transport_profile )
    STRUCT_DESCRIPTOR( HEVC_video )
    STRUCT_DESCRIPTOR( Extension )
    /*  */
    STRUCT_DESCRIPTOR( component )
    STRUCT_DESCRIPTOR( stream_identifier )
    STRUCT_DESCRIPTOR( CA_identifier )
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
    uint16_t            channel;
    uint8_t             layer;
    uint8_t             bit_depth;
} mpeg_stream_raw_info_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int64_t mpeg_pes_get_timestamp( uint8_t *time_stamp_data );

extern int mpeg_pes_check_steam_id_type( uint8_t *start_code, mpeg_pes_stream_id_type stream_id_type );

extern void mpeg_pes_get_header_info( uint8_t *buf, mpeg_pes_header_info_t *pes_info );

extern mpeg_pes_stream_id_type mpeg_pes_get_steam_id_type( mpeg_stream_group_type stream_judge );

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
    uint16_t                    descriptor_num,
    uint8_t                    *descriptor_data
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
