/*
 * MJPEG APP4 H264 demux bitstream filter
 * Copyright (c) 2020 Artem Pylypchuk
 *
 * Parts of this file are copied from guvcview project
 * guvcview              http://guvcview.sourceforge.net
 * Copyright Paulo Assis <pj.assis@gmail.com>                                    #
 *           Nobuhiro Iwamatsu <iwamatsu@nigauri.org>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * MJPEG APP4 H264 demux bitstream filter
 * demuxes H264 video from UVC webcamera MJPEG stream
 */

#include "avcodec.h"
#include "bsf.h"
#include "bytestream.h"
#include "mjpeg.h"
#include "h264.h"
#include "cbs.h"

typedef struct H264DemuxContext {
//    CodedBitstreamContext *input;
    CodedBitstreamContext *output;

    CodedBitstreamFragment *access_unit;

    AVCodecParameters *par_out;

    uint8_t *h264_last_IDR;             // last IDR frame retrieved from uvc h264 stream
    int h264_last_IDR_size;             // last IDR frame size
    uint8_t *h264_SPS;                  // h264 SPS info
    uint16_t h264_SPS_size;             // SPS size
    uint8_t *h264_PPS;                  // h264 PPS info
    uint16_t h264_PPS_size;             // PPS size
} H264DemuxContext;

/*
 * check buff (*buff) of size (size) for NALU type (type)
 * args:
 *    type - NALU type
 *    buff - buffer with MJPG uvc frame containing h264 data
 *    size - buffer size
 *
 * asserts:
 *    buff is not null
 *
 * returns: buffer pointer to NALU type data if found
 *          NULL if not found
 */
static uint8_t* check_NALU(uint8_t type, uint8_t *buff, int size)
{
  /*asserts*/
  assert(buff != NULL);

  uint8_t *sp = buff;
  uint8_t *nal = NULL;
  //search for NALU of type
  for(sp = buff; sp < buff + size - 5; ++sp)
  {
    if(sp[0] == 0x00 &&
       sp[1] == 0x00 &&
       sp[2] == 0x00 &&
       sp[3] == 0x01 &&
       (sp[4] & 0x1F) == type)
    {
      /*found it*/
      nal = sp + 4;
      break;
    }
  }

  return nal;
}

/*
 * parses a buff (*buff) of size (size) for NALU type (type)
 * args:
 *    type - NALU type
 *    NALU - pointer to pointer to NALU data
 *    buff - pointer to buffer containing h264 data muxed in MJPG container
 *    size - buff size
 *
 * asserts:
 *    buff is not null
 *
 * returns: NALU size and sets pointer (NALU) to NALU data
 *          -1 if no NALU found
 */
static int parse_NALU(uint8_t type, uint8_t **NALU, uint8_t *buff, int size)
{
  /*asserts*/
  assert(buff != NULL);

  int nal_size = 0;
  uint8_t *sp = NULL;

  //search for NALU of type
  uint8_t *nal = check_NALU(type, buff, size);
  if(nal == NULL)
  {
    fprintf(stderr, "V4L2_CORE: (uvc H264) could not find NALU of type %i in buffer\n", type);
    return -1;
  }

  //search for end of NALU
  for(sp = nal; sp < buff + size - 4; ++sp)
  {
    if(sp[0] == 0x00 &&
       sp[1] == 0x00 &&
       sp[2] == 0x00 &&
       sp[3] == 0x01)
    {
      nal_size = sp - nal;
      break;
    }
  }

  if(!nal_size)
    nal_size = buff + size - nal;

  *NALU = calloc(nal_size, sizeof(uint8_t));
  if(*NALU == NULL)
  {
    fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (parse_NALU): %s\n", strerror(errno));
    exit(-1);
  }
  memcpy(*NALU, nal, nal_size);

  //char test_filename2[20];
  //snprintf(test_filename2, 20, "frame_nalu-%i.raw", type);
  //SaveBuff (test_filename2, nal_size, *NALU);

  return nal_size;
}



static int mjpeg_demux_h264(AVBSFContext *ctx, AVPacket *out)
{
    AVPacket *in;
    int ret = 0;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    ret = av_new_packet(out, in->size);
    if (ret < 0)
        goto fail;

    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

    //original args
    uint8_t *h264_data = out->data;
    uint8_t *buff = in->data;
    int size = in->size;

    // original vars
    uint8_t *sp = NULL;
    uint8_t *spl= NULL;
    uint8_t *epl= NULL;
    uint8_t *header = NULL;
    uint8_t *ph264 = h264_data;

    //search for first APP4 marker
    for(sp = buff; sp < buff + size - 2; ++sp)
    {
      if(sp[0] == 0xFF &&
         sp[1] == APP4)
      {
        spl = sp + 2; //exclude APP4 marker
        break;
      }
    }

    if (spl == NULL) {
      av_log(ctx, AV_LOG_ERROR, "could not find APP4 marker in bitstream\n");
      goto fail;
    }

    /*(in big endian)
    *includes payload size + header + 6 bytes(2 length + 4 payload size)
    */
    uint16_t length = 0;
    length  = (uint16_t) spl[0] << 8;
    length |= (uint16_t) spl[1];

    header = spl + 2;
    /*in litle endian*/
    uint16_t header_length = header[2];
    header_length |= header[3] << 8;

    spl = header + header_length;
    /*in litle endian*/
    uint32_t payload_size = 0;
    payload_size =  ((uint32_t) spl[0]) << 0;
    payload_size |= ((uint32_t) spl[1]) << 8;
    payload_size |= ((uint32_t) spl[2]) << 16;
    payload_size |= ((uint32_t) spl[3]) << 24;

    spl += 4; /*start of payload*/
    epl = spl + payload_size; /*end of payload*/

    if(epl > buff + size)
    {
      av_log(ctx, AV_LOG_ERROR, "V4L2_CORE: payload size bigger than buffer, clipped to buffer size (demux_uvcH264)\n");
      epl = buff + size;
    }

    sp = spl;

    uint32_t max_seg_size = 64*1024;

    /*copy first segment*/
    length -= header_length + 6;

    if(length <= max_seg_size)
    {
      /*copy the segment to h264 buffer*/
      memcpy(ph264, sp, length);
      ph264 += length;
      sp += length;
    }

    /*copy other segments*/
    while( epl > sp)
    {
      if(sp[0] != 0xFF ||
         sp[1] != APP4)
      {
        av_log(ctx, AV_LOG_ERROR, "V4L2_CORE: expected APP4 marker but none found (demux_uvcH264)\n");
        goto exit;
      }
      else
      {
        length  = (uint16_t) sp[2] << 8;
        length |= (uint16_t) sp[3];

        length -= 2; /*remove the 2 bytes from length*/
      }

      sp += 4; /*APP4 marker + length*/

      if(length != max_seg_size) {
        av_log(ctx, AV_LOG_DEBUG, "V4L2_CORE: segment length is %i (demux_uvcH264)\n", length);
      }

      /*copy the segment to h264 buffer*/
      memcpy(ph264, sp, length);
      ph264 += length;
      sp += length;

      if((epl-sp) > 0 && (epl-sp < 4)) {
        av_log(ctx, AV_LOG_ERROR, "V4L2_CORE: payload ended unexpectedly (demux_uvcH264)\n");
        goto exit;
      }
    }

    if(epl-sp > 0) {
      av_log(ctx, AV_LOG_ERROR, "V4L2_CORE: copy segment with %i bytes (demux_uvcH264)\n", (int) (epl-sp));
      /*copy the remaining data*/
      memcpy(ph264, sp, epl-sp);
      ph264 += epl-sp;
    }

  exit:
    out->size = (ph264 - h264_data);
    av_packet_free(&in);

    if (!ctx->par_out->extradata) {
      H264DemuxContext *vd = ctx->priv_data;
      if(vd->h264_SPS == NULL)
      {
        vd->h264_SPS_size = parse_NALU( 7, &vd->h264_SPS,
                                        out->data,
                                        (int) out->size);

        if(vd->h264_SPS_size <= 0 || vd->h264_SPS == NULL)
        {
          av_log(ctx, AV_LOG_ERROR, "V4L2_CORE: (uvc H264) Could not find SPS (NALU type: 7)\n");
          return 0; //E_NO_DATA;
        }
        else
          av_log(ctx, AV_LOG_DEBUG, "V4L2_CORE: (uvc H264) stored SPS %i bytes of data\n",
                 vd->h264_SPS_size);
      }

      if(vd->h264_PPS == NULL)
      {
        vd->h264_PPS_size = parse_NALU( 8, &vd->h264_PPS,
                                        out->data,
                                        (int) out->size);

        if(vd->h264_PPS_size <= 0 || vd->h264_PPS == NULL)
        {
          av_log(ctx, AV_LOG_ERROR, "Could not find PPS (NALU type: 8)\n");
          return 0; //E_NO_DATA
        }
        else //if(verbosity > 0)
          av_log(ctx, AV_LOG_DEBUG, "V4L2_CORE: (uvc H264) stored PPS %i bytes of data\n",
                 vd->h264_PPS_size);
      }

      ctx->par_out->extradata_size = vd->h264_SPS_size + vd->h264_PPS_size + 1;
      ctx->par_out->extradata = malloc(ctx->par_out->extradata_size);
      memcpy(ctx->par_out->extradata, vd->h264_SPS, vd->h264_SPS_size);
      memcpy(ctx->par_out->extradata+vd->h264_SPS_size, vd->h264_PPS, vd->h264_PPS_size);

    }

    return 0;

  fail:
    av_packet_unref(out);
    av_packet_free(&in);
    return AVERROR_INVALIDDATA;
}

static int mjpeg_demux_h264_init(AVBSFContext *bsf)
{
  H264DemuxContext *ctx = bsf->priv_data;

  int err; //, i;
//
//  err = ff_cbs_init(&ctx->input, AV_CODEC_ID_MJPEG, bsf);
//  if (err < 0)
//    return err;
//
  err = ff_cbs_init(&ctx->output, AV_CODEC_ID_H264, bsf);
  if (err < 0)
    return err;

  //ctx->

  bsf->par_out->codec_type = AVMEDIA_TYPE_VIDEO;
  bsf->par_out->codec_tag = 0; //avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_YUV420P); //TODO fix hardcode
  bsf->par_out->codec_id = AV_CODEC_ID_H264;
  bsf->par_out->format = 0; //AV_PIX_FMT_YUV420P; //TODO fix hardcode
  bsf->par_out->bit_rate = 3000000;
  bsf->par_out->sample_aspect_ratio.den = 1;
  bsf->par_out->sample_aspect_ratio.num = 1;
  bsf->par_out->profile = 578;
  bsf->par_out->level = 40;

  //set H264 time_base
  //bsf->time_base_out.num = 1;
  //bsf->time_base_out.den = 60;


  return 0;
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_H264, AV_CODEC_ID_MJPEG, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_mjpeg_demux_h264_bsf = {
    .name      = "mjpeg_demux_h264",
    .priv_data_size = sizeof(H264DemuxContext),
    .init      = &mjpeg_demux_h264_init,
    .filter    = &mjpeg_demux_h264,
    .codec_ids = codec_ids,
};
