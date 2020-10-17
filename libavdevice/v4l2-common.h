/*
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

#ifndef AVDEVICE_V4L2_COMMON_H
#define AVDEVICE_V4L2_COMMON_H

#undef __STRICT_ANSI__ //workaround due to broken kernel headers
#include "config.h"
#include "libavformat/internal.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#if HAVE_SYS_VIDEOIO_H
#include <sys/videoio.h>
#else
#if HAVE_ASM_TYPES_H
#include <asm/types.h>
#endif
#include <linux/videodev2.h>
#endif
#include <linux/uvcvideo.h>
#include <linux/usb/video.h>
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "avdevice.h"
#include "timefilter.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "libavutil/avstring.h"

struct fmt_map {
    enum AVPixelFormat ff_fmt;
    enum AVCodecID codec_id;
    uint32_t v4l2_fmt;
};

#define UVC_H264_BMHINTS_RESOLUTION        (0x0001)
#define UVC_H264_BMHINTS_PROFILE           (0x0002)
#define UVC_H264_BMHINTS_RATECONTROL       (0x0004)
#define UVC_H264_BMHINTS_USAGE             (0x0008)
#define UVC_H264_BMHINTS_SLICEMODE         (0x0010)
#define UVC_H264_BMHINTS_SLICEUNITS        (0x0020)
#define UVC_H264_BMHINTS_MVCVIEW           (0x0040)
#define UVC_H264_BMHINTS_TEMPORAL          (0x0080)
#define UVC_H264_BMHINTS_SNR               (0x0100)
#define UVC_H264_BMHINTS_SPATIAL           (0x0200)
#define UVC_H264_BMHINTS_SPATIAL_RATIO     (0x0400)
#define UVC_H264_BMHINTS_FRAME_INTERVAL    (0x0800)
#define UVC_H264_BMHINTS_LEAKY_BKT_SIZE    (0x1000)
#define UVC_H264_BMHINTS_BITRATE           (0x2000)
#define UVC_H264_BMHINTS_ENTROPY           (0x4000)
#define UVC_H264_BMHINTS_IFRAMEPERIOD      (0x8000)

typedef enum _uvcx_control_selector_t
{
  UVCX_VIDEO_CONFIG_PROBE = 0x01,
  UVCX_VIDEO_CONFIG_COMMIT = 0x02,
  UVCX_RATE_CONTROL_MODE = 0x03,
  UVCX_TEMPORAL_SCALE_MODE = 0x04,
  UVCX_SPATIAL_SCALE_MODE = 0x05,
  UVCX_SNR_SCALE_MODE = 0x06,
  UVCX_LTR_BUFFER_SIZE_CONTROL = 0x07,
  UVCX_LTR_PICTURE_CONTROL = 0x08,
  UVCX_PICTURE_TYPE_CONTROL = 0x09,
  UVCX_VERSION = 0x0A,
  UVCX_ENCODER_RESET = 0x0B,
  UVCX_FRAMERATE_CONFIG = 0x0C,
  UVCX_VIDEO_ADVANCE_CONFIG = 0x0D,
  UVCX_BITRATE_LAYERS = 0x0E,
  UVCX_QP_STEPS_LAYERS = 0x0F,
} uvcx_control_selector_t;

/*
 * h264 probe commit struct (uvc 1.1)
 */
typedef struct _uvcx_video_config_probe_commit_t
{
  uint32_t dwFrameInterval;
  uint32_t dwBitRate;
  uint16_t bmHints;
  uint16_t wConfigurationIndex;
  uint16_t wWidth;
  uint16_t wHeight;
  uint16_t wSliceUnits;
  uint16_t wSliceMode;
  uint16_t wProfile;
  uint16_t wIFramePeriod;
  uint16_t wEstimatedVideoDelay;
  uint16_t wEstimatedMaxConfigDelay;
  uint8_t bUsageType;
  uint8_t bRateControlMode;
  uint8_t bTemporalScaleMode;
  uint8_t bSpatialScaleMode;
  uint8_t bSNRScaleMode;
  uint8_t bStreamMuxOption;
  uint8_t bStreamFormat;
  uint8_t bEntropyCABAC;
  uint8_t bTimestamp;
  uint8_t bNumOfReorderFrames;
  uint8_t bPreviewFlipped;
  uint8_t bView;
  uint8_t bReserved1;
  uint8_t bReserved2;
  uint8_t bStreamID;
  uint8_t bSpatialLayerRatio;
  uint16_t wLeakyBucketSize;
} __attribute__((packed)) uvcx_video_config_probe_commit_t;

/* encoder reset */
typedef struct _uvcx_encoder_reset
{
  uint16_t	wLayerID;
} __attribute__((__packed__)) uvcx_encoder_reset;

extern const struct fmt_map ff_fmt_conversion_table[];

uint32_t ff_fmt_ff2v4l(enum AVPixelFormat pix_fmt, enum AVCodecID codec_id);
enum AVPixelFormat ff_fmt_v4l2ff(uint32_t v4l2_fmt, enum AVCodecID codec_id);
enum AVCodecID ff_fmt_v4l2codec(uint32_t v4l2_fmt);
int make_uvc_xu_query(int fd, int unit_id, uint8_t selector, uint8_t query, uint8_t * data);

#endif /* AVDEVICE_V4L2_COMMON_H */
