/*****************************************************************************
 * d2v_parser.c
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
 * C. Moral rights of author belong to maki. Copyright abandons.
 *
 * D. Above three clauses are applied both to source and binary
 *   form of this software.
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <windows.h>
#include <inttypes.h>

#include "d2v_parser.h"

typedef struct {
    uint8_t         version;
    uint8_t         video_file_num;
    char          **filename;
} d2v_header_section_t;

typedef struct {
    uint8_t         stream_type;
    struct {
        uint16_t    video_pid;
        uint16_t    audio_pid;
        uint16_t    pcr_pid;
        uint8_t     packet_size;
    } mpeg2ts;
    uint8_t         mpeg_type;
    uint8_t         idct_algorithm;
    uint8_t         yuvrgb_scale;
    struct {
        uint16_t    gamma;
        uint16_t    offset;
    } luminance_filter;
    struct {
        uint16_t    left;
        uint16_t    right;
        uint16_t    top;
        uint16_t    bottom;
    } clipping;
    struct {
        float       width;
        float       height;
    } aspect_ratio;
    struct {
        uint16_t    width;
        uint16_t    height;
    } picture_size;
    uint8_t         field_operation;
    struct {
        uint16_t    rate;
        uint16_t    num;
        uint16_t    den;
    } frame_rate;
    struct {
        uint64_t    start_file;
        uint64_t    start_offset;
        uint64_t    end_file;
        uint64_t    end_offset;
    } location;
} d2v_setting_section_t;

typedef struct {
    uint8_t     closed;
    uint8_t     progr_seq;
} d2v_gop_info_t;

typedef struct {
    uint8_t     ref_prev_gop;
    uint8_t     progr;
    uint8_t     keyframe;
    uint8_t     tff;
    uint8_t     rff;
    uint8_t     gop_num;
} d2v_frame_info_t;

typedef struct {
    uint32_t            total_frames;
    uint32_t            rff_num;
    d2v_frame_info_t   *frames;
    d2v_gop_info_t     *gops;
} d2v_data_section_t;

typedef struct {
    d2v_header_section_t    header;
    d2v_setting_section_t   setting;
    d2v_data_section_t      data;
    uint32_t                order_total_frames;
} d2v_info_t;

#define BUFFER_SIZE         (2048)

static void release(void *h)
{
    d2v_info_t *info = (d2v_info_t *)h;
    if (!info)
        return;

    if (info->data.frames) {
        free(info->data.frames);
    }
    if (info->data.gops) {
        free(info->data.gops);
    }
    if (info->header.filename) {
        free(info->header.filename[0]);
        free(info->header.filename);
    }
    free(info);
}

#define SKIP_NEXT_LINE(buf, input, fail)        \
{                                               \
    if (!fgets(buf, sizeof(buf), input))        \
        goto fail;                              \
    else if (buf[0] == '\n')                    \
        break;                                  \
}
static void *parse(const char *input)
{
    if (!input)
        return NULL;

    FILE *d2v = fopen( input, "rt" );
    if (!d2v)
        return NULL;

    d2v_info_t *info = (d2v_info_t *)calloc(sizeof(d2v_info_t), 1);
    if (!info) {
        fclose(d2v);
        return NULL;
    }

    /* parse D2V file. */
    char buf[BUFFER_SIZE];
    int scan_num;

    /* D2V Format - Header Section */
    if (!fgets(buf, sizeof(buf), d2v) || !sscanf(buf, "DGIndexProjectFile%d", &scan_num))
        goto fail_parse;
    info->header.version = scan_num;
    if (!fgets(buf, sizeof(buf), d2v) || !sscanf(buf, "%d", &scan_num) || scan_num <= 0)
        goto fail_parse;
    info->header.video_file_num = scan_num;

    char *tmp = (char *)calloc(sizeof(char), info->header.video_file_num * BUFFER_SIZE);
    if (!tmp)
        goto fail_parse;
    char **tmp_p = (char **)calloc(sizeof(char*), info->header.video_file_num);
    if (!tmp_p) {
        free(tmp);
        goto fail_parse;
    }
    for (int i = 0; i < info->header.video_file_num; i++)
        *(tmp_p + i) = tmp + i * BUFFER_SIZE;
    info->header.filename = tmp_p;
    for (int i = 0; i < info->header.video_file_num; i++)
        if (!fgets(buf, sizeof(buf), d2v) || !sscanf(buf, "%s", info->header.filename[i]))
            goto fail_parse;

    while (1)
        SKIP_NEXT_LINE(buf, d2v, fail_parse)

    /* D2V Format - Setting Section */
    while (1) {
        SKIP_NEXT_LINE(buf, d2v, fail_parse)

        if (sscanf(buf, "Stream_Type=%d", &scan_num) == 1) {
            info->setting.stream_type = scan_num;
            continue;
        }
        if (sscanf(buf, "MPEG2_Transport_PID=%"SCNx16",%"SCNx16",%"SCNx16,
                   &(info->setting.mpeg2ts.video_pid), &(info->setting.mpeg2ts.audio_pid), &(info->setting.mpeg2ts.pcr_pid)) == 3)
            continue;
        if (sscanf(buf, "Transport_Packet_Size=%d", &scan_num) == 1) {
            info->setting.mpeg2ts.packet_size = scan_num;
            continue;
        }
        if (sscanf(buf, "MPEG_Type=%d", &scan_num) == 1) {
            info->setting.mpeg_type = scan_num;
            continue;
        }
        if (sscanf(buf, "iDCT_Algorithm=%d", &scan_num) == 1) {
            info->setting.idct_algorithm = scan_num;
            continue;
        }
        if (sscanf(buf, "YUVRGB_Scale=%d", &scan_num) == 1) {
            info->setting.yuvrgb_scale = scan_num;
            continue;
        }
        if (sscanf(buf, "Luminance_Filter=%"SCNu16",%"SCNu16, &(info->setting.luminance_filter.gamma), &(info->setting.luminance_filter.offset)) == 2)
            continue;
        if (sscanf(buf, "Clipping=%"SCNu16",%"SCNu16",%"SCNu16",%"SCNu16,
                   &(info->setting.clipping.left), &(info->setting.clipping.right), &(info->setting.clipping.top), &(info->setting.clipping.bottom)) == 4)
            continue;
        if (sscanf(buf, "Aspect_Ratio=%f:%f", &(info->setting.aspect_ratio.width), &(info->setting.aspect_ratio.height)) == 2)
            continue;
        if (sscanf(buf, "Picture_Size=%"SCNu16"x%"SCNu16, &(info->setting.picture_size.width), &(info->setting.picture_size.height)) == 2)
            continue;
        if (sscanf(buf, "Field_Operation=%d", &scan_num) == 1) {
            info->setting.field_operation = scan_num;
            continue;
        }
        if (sscanf(buf, "Frame_Rate=%"SCNu16" (%"SCNu16"/%"SCNu16")",
                   &(info->setting.frame_rate.rate), &(info->setting.frame_rate.num), &(info->setting.frame_rate.den)) == 3)
            continue;
        if (sscanf(buf, "Location=%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64,
                   &(info->setting.location.start_file), &(info->setting.location.start_offset),
                   &(info->setting.location.end_file), &(info->setting.location.end_offset)) == 4)
            continue;
    }

    /* D2V Format - Data Section */
    fpos_t data_section_start;
    fgetpos(d2v, &data_section_start);

    static const char *separater = " ";

    int gop_count = 0;
    int frame_count = 0;
    uint8_t flags = 0x00;
    while (flags != 0xFF) {
        if (!fgets(buf, sizeof(buf), d2v))
            goto fail_parse;
        char *token = strtok(buf, separater);
        if (!token)
            continue;
        int shift_count = 6;
        char flags_str[5] = "0xFF";
        while ((token = strtok(NULL, separater))) {
            if (shift_count)
            {
                shift_count--;
                continue;
            }
            flags_str[2] = token[0];
            flags_str[3] = token[1];
            flags = strtol(flags_str, NULL, 16);
            if (flags == 0xFF)
                break;
            frame_count++;
        }
        gop_count++;
    }
    if (frame_count == 0)
        goto fail_parse;

    info->data.gops = (d2v_gop_info_t *)calloc(sizeof(d2v_gop_info_t), gop_count);
    if (!info->data.gops)
        goto fail_parse;

    info->data.frames = (d2v_frame_info_t *)calloc(sizeof(d2v_frame_info_t), frame_count);
    if (!info->data.frames)
        goto fail_parse;

    fsetpos(d2v, &data_section_start);
    gop_count = 0;
    frame_count = 0;
    flags = 0x00;
    int enable_rff = (info->setting.field_operation == 0);
    int repeat_pict_count = 0;
    int rff_count = 0;
    int rff = 0;
    while (flags != 0xFF) {
        if (!fgets(buf, sizeof(buf), d2v))
            goto fail_parse;
        char *token = strtok(buf, separater);
        if (!token)
            continue;
        int gop_info;
        if (!sscanf(token, "%x", &gop_info))
            continue;
        info->data.gops[gop_count].closed    = !!(gop_info & 0x400);
        info->data.gops[gop_count].progr_seq = !!(gop_info & 0x200);
        int shift_count = 6;
        char flags_str[5] = "0xFF";
        while ((token = strtok(NULL, separater))) {
            if (shift_count)
            {
                shift_count--;
                continue;
            }
            flags_str[2] = token[0];
            flags_str[3] = token[1];
            flags = strtol(flags_str, NULL, 16);
            if (flags == 0xFF)
                break;
            info->data.frames[frame_count].ref_prev_gop =  !(flags & 0x80);
            info->data.frames[frame_count].progr        = !!(flags & 0x40);
            info->data.frames[frame_count].keyframe     =  ((flags & 0x30) == 0x10);
            info->data.frames[frame_count].tff          = !!(flags & 0x02);
            info->data.frames[frame_count].rff          = !!(flags & 0x01);
            info->data.frames[frame_count].gop_num      = gop_count;
            rff_count += info->data.frames[frame_count].rff;
            /* check RFF. */
            if (enable_rff && info->data.frames[frame_count].rff) {
                if (info->data.gops[gop_count].progr_seq) {
                    repeat_pict_count++;
                    if (info->data.frames[frame_count].tff)
                        repeat_pict_count++;
                } else if (info->data.frames[frame_count].progr) {
                    if (rff) {
                        repeat_pict_count++;
                    }
                    rff ^= 1;
                }
            }
            frame_count++;
        }
        gop_count++;
    }
    info->data.total_frames = frame_count;
    info->data.rff_num = rff_count;

    /* setup total frames. */
    info->order_total_frames = info->data.total_frames + repeat_pict_count;

    fclose(d2v);
    return info;

fail_parse:
    release(info);
    fclose(d2v);
    return NULL;
}
#undef SKIP_NEXT_LINE

static uint8_t *create_keyframe_list(void *h)
{
    d2v_info_t *info = (d2v_info_t *)h;
    if (!info)
        return NULL;

    /* check RFF. */
    int enable_rff = (info->setting.field_operation == 0);

    /* check total frames. */
    int total_frames = info->order_total_frames;

    /* create keyframe list. */
    uint8_t *keyframe_list= (uint8_t *)calloc(sizeof(uint8_t), total_frames);
    if (!keyframe_list)
        return NULL;

    for (int i = 0, order = 0, rff = 0; i < info->data.total_frames && order < total_frames; i++, order++) {
        keyframe_list[order] = info->data.frames[i].keyframe;
        if (enable_rff && info->data.frames[i].rff) {
            if (info->data.gops[info->data.frames[i].gop_num].progr_seq) {
                order++;
                keyframe_list[order] = info->data.frames[i].keyframe;
                if (info->data.frames[i].tff) {
                    order++;
                    keyframe_list[order] = info->data.frames[i].keyframe;
                }
            } else if (info->data.frames[i].progr) {
                if (rff) {
                    order++;
                    keyframe_list[order] = info->data.frames[i].keyframe;
                }
                rff ^= 1;
            }
        }
    }

    return keyframe_list;
}

static uint32_t get_total_frames(void *h)
{
    d2v_info_t *info = (d2v_info_t *)h;
    if (!info)
        return -1;
    return info->order_total_frames;
}

static const char *get_filename(void *h, int get_index)
{
    d2v_info_t *info = (d2v_info_t *)h;
    if (!info )
        return NULL;
    if (get_index >= info->header.video_file_num)
        return NULL;
    return info->header.filename[get_index];
}

d2v_parser_t d2v_parser = {
    parse,
    release,
    create_keyframe_list,
    get_total_frames,
    get_filename
};
