/*
 * Copyright 2008, The Android Open Source Project
 * Copyright 2010, Samsung Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "UVCCamera"

#include <utils/Log.h>

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <sys/poll.h>
#include "UVCCamera.h"
#include "cutils/properties.h"

using namespace android;

#define CHECK(return_value)                                          \
    if (return_value < 0) {                                          \
        ALOGE("%s::%d fail. errno: %s, m_camera_id = %d\n",           \
             __func__, __LINE__, strerror(errno), m_camera_id);      \
        return -1;                                                   \
    }


#define CHECK_PTR(return_value)                                      \
    if (return_value < 0) {                                          \
        ALOGE("%s::%d fail, errno: %s, m_camera_id = %d\n",           \
             __func__,__LINE__, strerror(errno), m_camera_id);       \
        return NULL;                                                 \
    }

#define ALIGN_TO_32B(x)   ((((x) + (1 <<  5) - 1) >>  5) <<  5)
#define ALIGN_TO_128B(x)  ((((x) + (1 <<  7) - 1) >>  7) <<  7)
#define ALIGN_TO_8KB(x)   ((((x) + (1 << 13) - 1) >> 13) << 13)

namespace android {

// ======================================================================
// Camera controls

static struct timeval time_start;
static struct timeval time_stop;

unsigned long measure_time(struct timeval *start, struct timeval *stop)
{
    unsigned long sec, usec, time;

    sec = stop->tv_sec - start->tv_sec;

    if (stop->tv_usec >= start->tv_usec) {
        usec = stop->tv_usec - start->tv_usec;
    } else {
        usec = stop->tv_usec + 1000000 - start->tv_usec;
        sec--;
    }

    time = (sec * 1000000) + usec;

    return time;
}

static int get_pixel_depth(unsigned int fmt)
{
    int depth = 0;

    switch (fmt) {
    case V4L2_PIX_FMT_NV12:
        depth = 12;
        break;
    case V4L2_PIX_FMT_NV12T:
        depth = 12;
        break;
    case V4L2_PIX_FMT_NV21:
        depth = 12;
        break;
    case V4L2_PIX_FMT_YUV420:
        depth = 12;
        break;

    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_YVYU:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_VYUY:
    case V4L2_PIX_FMT_NV16:
    case V4L2_PIX_FMT_NV61:
    case V4L2_PIX_FMT_YUV422P:
        depth = 16;
        break;

    case V4L2_PIX_FMT_RGB32:
        depth = 32;
        break;
    }

    return depth;
}

#define ALIGN_W(x)      (((x) + 0x7F) & (~0x7F))    // Set as multiple of 128
#define ALIGN_H(x)      (((x) + 0x1F) & (~0x1F))    // Set as multiple of 32
#define ALIGN_BUF(x)    (((x) + 0x1FFF)& (~0x1FFF)) // Set as multiple of 8K

static int fimc_poll(struct pollfd *events)
{
    int ret;

    /* 10 second delay is because sensor can take a long time
     * to do auto focus and capture in dark settings
     */
    ret = poll(events, 1, 10000);
    if (ret < 0) {
        ALOGE("ERR(%s):poll error\n", __func__);
        return ret;
    }

    if (ret == 0) {
        ALOGE("ERR(%s):No data in 10 secs..\n", __func__);
        return ret;
    }

    return ret;
}

int UVCCamera::previewPoll(bool preview)
{
    return fimc_poll(&m_events_c);
}

static int fimc_v4l2_querycap(int fp)
{
    struct v4l2_capability cap;
    int ret = 0;

    ret = ioctl(fp, VIDIOC_QUERYCAP, &cap);

    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_QUERYCAP failed\n", __func__);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        ALOGE("ERR(%s):no capture devices\n", __func__);
        return -1;
    }

    return ret;
}

static const __u8* fimc_v4l2_enuminput(int fp, int index)
{
    static struct v4l2_input input;

    input.index = index;
    if (ioctl(fp, VIDIOC_ENUMINPUT, &input) != 0) {
        ALOGE("ERR(%s):No matching index found\n", __func__);
        return NULL;
    }
    ALOGI("Name of input channel[%d] is %s\n", input.index, input.name);

    return input.name;
}


static int fimc_v4l2_s_input(int fp, int index)
{
    struct v4l2_input input;
    int ret;

    input.index = index;

    ret = ioctl(fp, VIDIOC_S_INPUT, &input);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_INPUT failed\n", __func__);
        return ret;
    }

    return ret;
}

static int fimc_v4l2_s_fmt(int fp, int width, int height, unsigned int fmt, int flag_capture)
{
    struct v4l2_format v4l2_fmt;
    struct v4l2_pix_format pixfmt;
    int ret;

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    memset(&pixfmt, 0, sizeof(pixfmt));

    pixfmt.width = width;
    pixfmt.height = height;
    pixfmt.pixelformat = fmt;

    pixfmt.sizeimage = (width * height * get_pixel_depth(fmt)) / 8;

    pixfmt.field = V4L2_FIELD_NONE;

    v4l2_fmt.fmt.pix = pixfmt;

    /* Set up for capture */
    ret = ioctl(fp, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_FMT failed\n", __func__);
        return -1;
    }

    return 0;
}

static int fimc_v4l2_s_fmt_cap(int fp, int width, int height, unsigned int fmt)
{
    struct v4l2_format v4l2_fmt;
    struct v4l2_pix_format pixfmt;
    int ret;

    memset(&pixfmt, 0, sizeof(pixfmt));

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    pixfmt.width = width;
    pixfmt.height = height;
    pixfmt.pixelformat = fmt;
    if (fmt == V4L2_PIX_FMT_JPEG) {
        pixfmt.colorspace = V4L2_COLORSPACE_JPEG;
    }

    pixfmt.sizeimage = (width * height * get_pixel_depth(fmt)) / 8;

    v4l2_fmt.fmt.pix = pixfmt;

    //ALOGE("ori_w %d, ori_h %d, w %d, h %d\n", width, height, v4l2_fmt.fmt.pix.width, v4l2_fmt.fmt.pix.height);

    /* Set up for capture */
    ret = ioctl(fp, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_FMT failed\n", __func__);
        return ret;
    }

    return ret;
}

static int fimc_v4l2_enum_fmt(int fp, unsigned int fmt)
{
    struct v4l2_fmtdesc fmtdesc;
    int found = 0;

    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;

    while (ioctl(fp, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        ALOGD("supported pixel format: (%x) %s\n", fmtdesc.pixelformat, fmtdesc.description);
        if (fmtdesc.pixelformat == fmt) {
            found = 1;
            break;
        }

        fmtdesc.index++;
    }

    if (!found) {
        ALOGE("unsupported pixel format\n");
        return -1;
    }

    return 0;
}

static int fimc_v4l2_enum_framesize(int fp, unsigned int pixel_format, unsigned int index,
                                    unsigned *width, unsigned *height)
{
    struct v4l2_frmsizeenum fsize;

    fsize.pixel_format = pixel_format;
    fsize.index = index;

    if(ioctl(fp, VIDIOC_ENUM_FRAMESIZES, &fsize) == 0) {
        // UVC devices always use discrete frame sizes
        *width = fsize.discrete.width;
        *height = fsize.discrete.height;

        return 0;
    }
    else {
        return -1;
    }
}

static int fimc_v4l2_reqbufs(int fp, enum v4l2_buf_type type, unsigned nr_bufs)
{
    struct v4l2_requestbuffers req;
    int ret;

    memset(&req, 0, sizeof(req));
    req.count = nr_bufs;
    req.type = type;
    req.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(fp, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_REQBUFS failed\n", __func__);
        return -1;
    }

    if(req.count != nr_bufs) {
      ALOGE("ERR(%s):Wrong number of buffers (%d)\n", __func__, req.count);
        return -1;
    }

    return req.count;
}

static int fimc_v4l2_querybuf(int fp, int index, struct fimc_buffer *buffer, enum v4l2_buf_type type)
{
    struct v4l2_buffer v4l2_buf;
    int ret;

    ALOGI("%s :", __func__);

    memset(&v4l2_buf, 0, sizeof(v4l2_buf));
    v4l2_buf.type = type;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;
    v4l2_buf.index = index;

    ret = ioctl(fp , VIDIOC_QUERYBUF, &v4l2_buf);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_QUERYBUF failed\n", __func__);
        return -1;
    }

    buffer->length = v4l2_buf.length;
    if ((buffer->start = (char *)mmap(0, v4l2_buf.length,
                                         PROT_READ | PROT_WRITE, MAP_SHARED,
                                         fp, v4l2_buf.m.offset)) < 0) {
         ALOGE("%s %d] mmap() failed\n",__func__, __LINE__);
         return -1;
    }

    ALOGI("%s: buffer->start = %p v4l2_buf.length = %d",
         __func__, buffer->start, v4l2_buf.length);

    return 0;
}

static void fimc_v4l2_querybufs(int fp, struct fimc_buffer *buffer, int bufcount, enum v4l2_buf_type type)
{
    for(int i = 0; i < bufcount; i++)
        fimc_v4l2_querybuf(fp, i, buffer + i, type);
}

static int fimc_v4l2_streamon(int fp)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret;

    ret = ioctl(fp, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_STREAMON failed\n", __func__);
        return ret;
    }

    return ret;
}

static int fimc_v4l2_streamoff(int fp)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret;

    ALOGV("%s :", __func__);
    ret = ioctl(fp, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_STREAMOFF failed\n", __func__);
        return ret;
    }

    return ret;
}

static int fimc_v4l2_qbuf(int fp, int index)
{
    struct v4l2_buffer v4l2_buf;
    int ret;

    memset(&v4l2_buf, 0, sizeof(v4l2_buf));
    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;
    v4l2_buf.index = index;

    ret = ioctl(fp, VIDIOC_QBUF, &v4l2_buf);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_QBUF failed\n", __func__);
        return ret;
    }

    return 0;
}

static int fimc_v4l2_dqbuf(int fp)
{
    struct v4l2_buffer v4l2_buf;
    int ret;

    memset(&v4l2_buf, 0, sizeof(v4l2_buf));
    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(fp, VIDIOC_DQBUF, &v4l2_buf);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_DQBUF failed, dropped frame (%d)\n", __func__, ret);
        return ret;
    }

    return v4l2_buf.index;
}

static int fimc_v4l2_g_ctrl(int fp, unsigned int id)
{
    struct v4l2_control ctrl;
    int ret;

    ctrl.id = id;

    ret = ioctl(fp, VIDIOC_G_CTRL, &ctrl);
    if (ret < 0) {
        ALOGE("ERR(%s): VIDIOC_G_CTRL(id = 0x%x (%d)) failed, ret = %d\n",
             __func__, id, id-V4L2_CID_PRIVATE_BASE, ret);
        return ret;
    }

    return ctrl.value;
}

static int fimc_v4l2_s_ctrl(int fp, unsigned int id, unsigned int value)
{
    struct v4l2_control ctrl;
    int ret;

    ctrl.id = id;
    ctrl.value = value;

    if(id >= V4L2_CID_PRIVATE_BASE) {
        ALOGE("IGNORE(%s):VIDIOC_S_CTRL(id = %#x (%d), value = %d)\n",
             __func__, id, id-V4L2_CID_PRIVATE_BASE, value);
        return 0;
    }

    ret = ioctl(fp, VIDIOC_S_CTRL, &ctrl);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_CTRL(id = %#x (%d), value = %d) failed ret = %d\n",
             __func__, id, id-V4L2_CID_PRIVATE_BASE, value, ret);

        return ret;
    }

    return ctrl.value;
}

static int fimc_v4l2_s_ext_ctrl(int fp, unsigned int id, void *value)
{
    struct v4l2_ext_controls ctrls;
    struct v4l2_ext_control ctrl;
    int ret;

    ctrl.id = id;
    //???
    ctrl.reserved2[0] = (__u32)value;

    ctrls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
    ctrls.count = 1;
    ctrls.controls = &ctrl;

    ret = ioctl(fp, VIDIOC_S_EXT_CTRLS, &ctrls);
    if (ret < 0)
        ALOGE("ERR(%s):VIDIOC_S_EXT_CTRLS failed\n", __func__);

    return ret;
}

static int fimc_v4l2_g_parm(int fp, struct v4l2_streamparm *streamparm)
{
    int ret;

    streamparm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fp, VIDIOC_G_PARM, streamparm);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_G_PARM failed\n", __func__);
        return -1;
    }

    ALOGV("%s : timeperframe: numerator %d, denominator %d\n", __func__,
            streamparm->parm.capture.timeperframe.numerator,
            streamparm->parm.capture.timeperframe.denominator);

    return 0;
}

static int fimc_v4l2_s_parm(int fp, struct v4l2_streamparm *streamparm)
{
    int ret;

    streamparm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fp, VIDIOC_S_PARM, streamparm);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_PARM failed\n", __func__);
        return ret;
    }

    return 0;
}

// ======================================================================
// Constructor & Destructor

UVCCamera::UVCCamera() :
            m_flag_init(0),
            m_camera_id(CAMERA_ID_FRONT),
            m_cam_fd(-1),
            //m_cam_fd2(-1),
            m_preview_v4lformat(V4L2_PIX_FMT_YUV422P),
            m_preview_width      (0),
            m_preview_height     (0),
            m_preview_max_width  (0),
            m_preview_max_height (0),
            m_snapshot_v4lformat(-1),
            m_snapshot_width      (0),
            m_snapshot_height     (0),
            m_snapshot_max_width  (MAX_FRONT_CAMERA_SNAPSHOT_WIDTH),
            m_snapshot_max_height (MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT),
            m_angle(-1),
            m_anti_banding(-1),
            m_wdr(-1),
            m_anti_shake(-1),
            m_zoom_level(-1),
            m_object_tracking(-1),
            m_smart_auto(-1),
            m_beauty_shot(-1),
            m_vintage_mode(-1),
            m_face_detect(-1),
            m_gps_enabled(false),
            m_gps_latitude(-1),
            m_gps_longitude(-1),
            m_gps_altitude(-1),
            m_gps_timestamp(-1),
            m_vtmode(0),
            m_sensor_mode(-1),
            m_shot_mode(-1),
            m_exif_orientation(-1),
            m_blur_level(-1),
            m_chk_dataline(-1),
            m_video_gamma(-1),
            m_slow_ae(-1),
            m_camera_af_flag(-1),
            m_flag_camera_start(0),
            m_jpeg_thumbnail_width (0),
            m_jpeg_thumbnail_height(0),
            m_jpeg_quality(100)
{
    struct v4l2_captureparm capture;

    memset(&m_capture_buf, 0, sizeof(m_capture_buf));

    ALOGV("%s :", __func__);
}

UVCCamera::~UVCCamera()
{
    ALOGV("%s :", __func__);
}

const char *UVCCamera::getPreviewSizes() {
    return m_frame_size_string;
}

int UVCCamera::initCamera(int index)
{
    ALOGV("%s :", __func__);
    int ret = 0;

    if (!m_flag_init) {
        /* Arun C
         * Reset the lense position only during camera starts; don't do
         * reset between shot to shot
         */
        m_camera_af_flag = -1;

        m_cam_fd = open(CAMERA_DEV_NAME, O_RDWR);
        if (m_cam_fd < 0) {
            ALOGE("ERR(%s):Cannot open %s (error : %s)\n", __func__, CAMERA_DEV_NAME, strerror(errno));
            return -1;
        }
        ALOGV("%s: open(%s) --> m_cam_fd %d", __FUNCTION__, CAMERA_DEV_NAME, m_cam_fd);

        ALOGE("initCamera: m_cam_fd(%d), m_jpeg_fd(%d)", m_cam_fd, m_jpeg_fd);

        ret = fimc_v4l2_querycap(m_cam_fd);
        CHECK(ret);
        if (!fimc_v4l2_enuminput(m_cam_fd, index))
            return -1;
        ret = fimc_v4l2_s_input(m_cam_fd, index);
        CHECK(ret);

        m_camera_id = index;

        // Find the framesizes we can handle
        m_preview_max_height = m_preview_max_width = 0;
        m_frame_size_string[0] = '\0';
        char *sizeStr = m_frame_size_string;
        unsigned w, h;
        for(int sindex = 0;
                fimc_v4l2_enum_framesize(m_cam_fd, V4L2_PIX_FMT_YUYV, sindex, &w, &h) == 0;
                sindex++) {
            ALOGD("Adding %d,%d\n", w, h);
            // FIXME - KR found string overrun.
            int ret = sprintf(sizeStr, "%s%dx%d", (sindex == 0 ? "" : ","), w, h);
            if(ret >= 0) {
                sizeStr += ret;
            }

            if(w > m_preview_max_width)
                m_preview_max_width = w;

            if(h > m_preview_max_height)
                m_preview_max_height = h;
        }

        m_snapshot_max_width  = MAX_FRONT_CAMERA_SNAPSHOT_WIDTH;
        m_snapshot_max_height = MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT;

        setExifFixedAttribute();

        m_flag_init = 1;
        ALOGI("%s : initialized", __FUNCTION__);
    }
    return 0;
}

void UVCCamera::resetCamera()
{
    ALOGV("%s :", __func__);
    DeinitCamera();
    initCamera(m_camera_id);
}

void UVCCamera::DeinitCamera()
{
    ALOGV("%s :", __func__);

    if (m_flag_init) {

        stopRecord();

        /* close m_cam_fd after stopRecord() because stopRecord()
         * uses m_cam_fd to change frame rate
         */
        ALOGI("DeinitCamera: m_cam_fd(%d)", m_cam_fd);
        if (m_cam_fd > -1) {
            close(m_cam_fd);
            m_cam_fd = -1;
        }

#if 0
        ALOGI("DeinitCamera: m_cam_fd2(%d)", m_cam_fd2);
        if (m_cam_fd2 > -1) {
            close(m_cam_fd2);
            m_cam_fd2 = -1;
        }
#endif

        m_flag_init = 0;
    }
    else ALOGI("%s : already deinitialized", __FUNCTION__);
}


int UVCCamera::getCameraFd(void)
{
    return m_cam_fd;
}

// ======================================================================
// Preview

int UVCCamera::startPreview(void)
{
    ALOGV("%s :", __func__);

    // aleady started
    if (m_flag_camera_start > 0) {
        ALOGE("ERR(%s):Preview was already started\n", __func__);
        return 0;
    }

    if (m_cam_fd <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    memset(&m_events_c, 0, sizeof(m_events_c));
    m_events_c.fd = m_cam_fd;
    m_events_c.events = POLLIN | POLLERR;

    /* enum_fmt, s_fmt sample */
    int ret = fimc_v4l2_enum_fmt(m_cam_fd,m_preview_v4lformat);
    CHECK(ret);
    ret = fimc_v4l2_s_fmt(m_cam_fd, m_preview_width,m_preview_height,m_preview_v4lformat, 0);
    CHECK(ret);

    ret = fimc_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, MAX_BUFFERS);
    CHECK(ret);

    ALOGV("%s : m_preview_width: %d m_preview_height: %d m_angle: %d\n",
            __func__, m_preview_width, m_preview_height, m_angle);

    /* start with all buffers in queue */
    for (int i = 0; i < MAX_BUFFERS; i++) {
        ret = fimc_v4l2_qbuf(m_cam_fd, i);
        CHECK(ret);
    }

    ret = fimc_v4l2_s_parm(m_cam_fd, &m_streamparm);
    CHECK(ret);

    ret = fimc_v4l2_streamon(m_cam_fd);
    CHECK(ret);

    m_flag_camera_start = 1;

    // It is a delay for a new frame, not to show the previous bigger ugly picture frame.
    ret = fimc_poll(&m_events_c);
    CHECK(ret);

    ALOGV("%s: got the first frame of the preview\n", __func__);

    return 0;
}

int UVCCamera::stopPreview(void)
{
    int ret;

    ALOGV("%s :", __func__);

    if (m_flag_camera_start == 0) {
        ALOGW("%s: doing nothing because m_flag_camera_start is zero", __func__);
        return 0;
    }

    if (m_cam_fd <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    ret = fimc_v4l2_streamoff(m_cam_fd);
    CHECK(ret);

    m_flag_camera_start = 0;

    return ret;
}

//Recording
int UVCCamera::startRecord(void)
{
    int ret, i;

    ALOGV("%s :", __func__);

    // aleady started
    if (m_flag_record_start > 0) {
        ALOGE("ERR(%s):Preview was already started\n", __func__);
        return 0;
    }

    ALOGE("ERR(%s):kevin fixme record busted\n", __func__);
    return -1;

#if 0
    if (m_cam_fd2 <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    /* enum_fmt, s_fmt sample */
    ret = fimc_v4l2_enum_fmt(m_cam_fd2, V4L2_PIX_FMT_NV12T);
    CHECK(ret);

    ALOGI("%s: m_recording_width = %d, m_recording_height = %d\n",
         __func__, m_recording_width, m_recording_height);

    ret = fimc_v4l2_s_fmt(m_cam_fd2, m_recording_width,
                          m_recording_height, V4L2_PIX_FMT_NV12T, 0);
    CHECK(ret);

    ret = fimc_v4l2_reqbufs(m_cam_fd2, V4L2_BUF_TYPE_VIDEO_CAPTURE, MAX_BUFFERS);
    CHECK(ret);

    /* start with all buffers in queue */
    for (i = 0; i < MAX_BUFFERS; i++) {
        ret = fimc_v4l2_qbuf(m_cam_fd2, i);
        CHECK(ret);
    }

    ret = fimc_v4l2_streamon(m_cam_fd2);
    CHECK(ret);

    // Get and throw away the first frame since it is often garbled.
    memset(&m_events_c2, 0, sizeof(m_events_c2));
    m_events_c2.fd = m_cam_fd2;
    m_events_c2.events = POLLIN | POLLERR;
    ret = fimc_poll(&m_events_c2);
    CHECK(ret);

    m_flag_record_start = 1;
#endif

    return 0;
}

int UVCCamera::stopRecord(void)
{
    int ret;

    ALOGV("%s :", __func__);

    if (m_flag_record_start == 0) {
        ALOGW("%s: doing nothing because m_flag_record_start is zero", __func__);
        return 0;
    }

#if 0 // FIXME - disabled

    if (m_cam_fd2 <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    m_flag_record_start = 0;

    ret = fimc_v4l2_streamoff(m_cam_fd2);
    CHECK(ret);

#endif

    return 0;
}


void UVCCamera::pausePreview()
{
    fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_STREAM_PAUSE, 0);
}

int UVCCamera::getPreview()
{
    int index;

    index = fimc_v4l2_dqbuf(m_cam_fd);
    if (!(0 <= index && index < MAX_BUFFERS)) {
        ALOGE("ERR(%s):wrong index = %d\n", __func__, index);
        return -1;
    }

    return index;
}

int UVCCamera::getRecordFrame()
{
    if (m_flag_record_start == 0) {
        ALOGE("%s: m_flag_record_start is 0", __func__);
        return -1;
    }

#if 0 // FIXME
    previewPoll(false);
    return fimc_v4l2_dqbuf(m_cam_fd2);
#else
    return -1;
#endif
}

int UVCCamera::releaseFrame(int index)
{
    return fimc_v4l2_qbuf(m_cam_fd, index);
}

int UVCCamera::setPreviewSize(int width, int height, int pixel_format)
{
    ALOGV("%s(width(%d), height(%d), format(%d))", __func__, width, height, pixel_format);

    int v4lpixelformat = pixel_format;

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
    if (v4lpixelformat == V4L2_PIX_FMT_YUV420)
        ALOGV("PreviewFormat:V4L2_PIX_FMT_YUV420");
    else if (v4lpixelformat == V4L2_PIX_FMT_NV12)
        ALOGV("PreviewFormat:V4L2_PIX_FMT_NV12");
    else if (v4lpixelformat == V4L2_PIX_FMT_NV12T)
        ALOGV("PreviewFormat:V4L2_PIX_FMT_NV12T");
    else if (v4lpixelformat == V4L2_PIX_FMT_NV21)
        ALOGV("PreviewFormat:V4L2_PIX_FMT_NV21");
    else if (v4lpixelformat == V4L2_PIX_FMT_YUV422P)
        ALOGV("PreviewFormat:V4L2_PIX_FMT_YUV422P");
    else if (v4lpixelformat == V4L2_PIX_FMT_YUYV)
        ALOGV("PreviewFormat:V4L2_PIX_FMT_YUYV");
    else if (v4lpixelformat == V4L2_PIX_FMT_RGB565)
        ALOGV("PreviewFormat:V4L2_PIX_FMT_RGB565");
    else
        ALOGV("PreviewFormat:UnknownFormat");
#endif
    m_preview_width  = width;
    m_preview_height = height;
    m_preview_v4lformat = v4lpixelformat;

    return 0;
}

int UVCCamera::getPreviewSize(int *width, int *height, int *frame_size)
{
    *width  = m_preview_width;
    *height = m_preview_height;
    *frame_size = m_frameSize(m_preview_v4lformat, m_preview_width, m_preview_height);

    return 0;
}

int UVCCamera::getPreviewMaxSize(int *width, int *height)
{
    *width  = m_preview_max_width;
    *height = m_preview_max_height;

    return 0;
}

int UVCCamera::getPreviewPixelFormat(void)
{
    return m_preview_v4lformat;
}


// ======================================================================
// Snapshot
/*
 * Devide getJpeg() as two funcs, setSnapshotCmd() & getJpeg() because of the shutter sound timing.
 * Here, just send the capture cmd to camera ISP to start JPEG capture.
 */
int UVCCamera::setSnapshotCmd(void)
{
    ALOGV("%s :", __func__);

    int ret = 0;

    LOG_TIME_DEFINE(0)
    LOG_TIME_DEFINE(1)

    if (m_cam_fd <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return 0;
    }

    if (m_flag_camera_start > 0) {
        LOG_TIME_START(0)
        ALOGW("WARN(%s):Camera was in preview, should have been stopped\n", __func__);
        stopPreview();
        LOG_TIME_END(0)
    }

    memset(&m_events_c, 0, sizeof(m_events_c));
    m_events_c.fd = m_cam_fd;
    m_events_c.events = POLLIN | POLLERR;

    LOG_TIME_START(1) // prepare
    int nframe = 1;

    ret = fimc_v4l2_enum_fmt(m_cam_fd,m_snapshot_v4lformat);
    CHECK(ret);
    ret = fimc_v4l2_s_fmt_cap(m_cam_fd, m_snapshot_width, m_snapshot_height, V4L2_PIX_FMT_JPEG);
    CHECK(ret);
    ret = fimc_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, nframe);
    CHECK(ret);
    fimc_v4l2_querybufs(m_cam_fd, m_capture_buf, nframe, V4L2_BUF_TYPE_VIDEO_CAPTURE);

    // FIXME kevinh - should queue up more frames
    ret = fimc_v4l2_qbuf(m_cam_fd, 0);
    CHECK(ret);

    ret = fimc_v4l2_streamon(m_cam_fd);
    CHECK(ret);
    LOG_TIME_END(1)

    return 0;
}

int UVCCamera::endSnapshot(void)
{
    int ret;

    ALOGI("%s :", __func__);
    struct fimc_buffer *buf = m_capture_buf;
    if (buf->start) {
        munmap(buf->start, buf->length);
        ALOGI("munmap():virt. addr %p size = %d\n",
             buf->start, buf->length);
        buf->start = NULL;
        buf->length = 0;
    }
    return 0;
}

/*
 * Set Jpeg quality & exif info and get JPEG data from camera ISP
 */
unsigned char* UVCCamera::getJpeg(int *jpeg_size, unsigned int *phyaddr)
{
    ALOGV("%s :", __func__);

    int index, ret = 0;
    unsigned char *addr;

    LOG_TIME_DEFINE(2)

    // capture
    ret = fimc_poll(&m_events_c);
    CHECK_PTR(ret);
    index = fimc_v4l2_dqbuf(m_cam_fd);
    if (index != 0) {
        ALOGE("ERR(%s):wrong index = %d\n", __func__, index);
        return NULL;
    }

    *jpeg_size = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_MAIN_SIZE);
    CHECK_PTR(*jpeg_size);

    int main_offset = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_MAIN_OFFSET);
    CHECK_PTR(main_offset);
    m_postview_offset = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_POSTVIEW_OFFSET);
    CHECK_PTR(m_postview_offset);

    ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_STREAM_PAUSE, 0);
    CHECK_PTR(ret);
    ALOGV("\nsnapshot dqueued buffer = %d snapshot_width = %d snapshot_height = %d, size = %d\n\n",
            index, m_snapshot_width, m_snapshot_height, *jpeg_size);

    addr = (unsigned char*)(m_capture_buf[0].start) + main_offset;
    *phyaddr = 0; // FIXME kevinh - not correct

    LOG_TIME_START(2) // post
    ret = fimc_v4l2_streamoff(m_cam_fd);
    CHECK_PTR(ret);
    LOG_TIME_END(2)

    return addr;
}

int UVCCamera::getExif(unsigned char *pExifDst, unsigned char *pThumbSrc)
{
    JpegEncoder jpgEnc;

    ALOGV("%s : m_jpeg_thumbnail_width = %d, height = %d",
         __func__, m_jpeg_thumbnail_width, m_jpeg_thumbnail_height);
    if ((m_jpeg_thumbnail_width > 0) && (m_jpeg_thumbnail_height > 0)) {
        int inFormat = JPG_MODESEL_YCBCR;
        int outFormat = JPG_422;
        switch (m_snapshot_v4lformat) {
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_NV21:
        case V4L2_PIX_FMT_NV12T:
        case V4L2_PIX_FMT_YUV420:
            outFormat = JPG_420;
            break;
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_YUV422P:
            outFormat = JPG_422;
            break;
        }

        if (jpgEnc.setConfig(JPEG_SET_ENCODE_IN_FORMAT, inFormat) != JPG_SUCCESS)
            return -1;

        if (jpgEnc.setConfig(JPEG_SET_SAMPING_MODE, outFormat) != JPG_SUCCESS)
            return -1;

        if (jpgEnc.setConfig(JPEG_SET_ENCODE_QUALITY, JPG_QUALITY_LEVEL_2) != JPG_SUCCESS)
            return -1;

        int thumbWidth, thumbHeight, thumbSrcSize;
        getThumbnailConfig(&thumbWidth, &thumbHeight, &thumbSrcSize);
        if (jpgEnc.setConfig(JPEG_SET_ENCODE_WIDTH, thumbWidth) != JPG_SUCCESS)
            return -1;

        if (jpgEnc.setConfig(JPEG_SET_ENCODE_HEIGHT, thumbHeight) != JPG_SUCCESS)
            return -1;

        char *pInBuf = (char *)jpgEnc.getInBuf(thumbSrcSize);
        if (pInBuf == NULL)
            return -1;
        memcpy(pInBuf, pThumbSrc, thumbSrcSize);

        unsigned int thumbSize;

        jpgEnc.encode(&thumbSize, NULL);

        ALOGV("%s : enableThumb set to true", __func__);
        mExifInfo.enableThumb = true;
    } else {
        ALOGV("%s : enableThumb set to false", __func__);
        mExifInfo.enableThumb = false;
    }

    unsigned int exifSize;

    setExifChangedAttribute();

    ALOGV("%s: calling jpgEnc.makeExif, mExifInfo.width set to %d, height to %d\n",
         __func__, mExifInfo.width, mExifInfo.height);

    jpgEnc.makeExif(pExifDst, &mExifInfo, &exifSize, true);

    return exifSize;
}

void UVCCamera::getPostViewConfig(int *width, int *height, int *size)
{
    *width = m_preview_max_width;
    *height = m_preview_max_height;
    *size = *width * *height * VGA_THUMBNAIL_BPP / 8; // kevinh FIXME, not quite right

    ALOGV("[5B] m_preview_width : %d, mPostViewWidth = %d mPostViewHeight = %d mPostViewSize = %d",
            m_preview_width, *width, *height, *size);
}

void UVCCamera::getThumbnailConfig(int *width, int *height, int *size)
{
        *width  = FRONT_CAMERA_THUMBNAIL_WIDTH;
        *height = FRONT_CAMERA_THUMBNAIL_HEIGHT;
        *size   = FRONT_CAMERA_THUMBNAIL_WIDTH * FRONT_CAMERA_THUMBNAIL_HEIGHT
                    * FRONT_CAMERA_THUMBNAIL_BPP / 8;
}

int UVCCamera::getPostViewOffset(void)
{
    return m_postview_offset;
}

int UVCCamera::getSnapshotAndJpeg(unsigned char *yuv_buf, unsigned char *jpeg_buf,
                                            unsigned int *output_size)
{
    ALOGV("%s :", __func__);

    int index;
    //unsigned int addr;
    unsigned char *addr;
    int ret = 0;

    LOG_TIME_DEFINE(0)
    LOG_TIME_DEFINE(1)
    LOG_TIME_DEFINE(2)
    LOG_TIME_DEFINE(3)
    LOG_TIME_DEFINE(4)
    LOG_TIME_DEFINE(5)

    //fimc_v4l2_streamoff(m_cam_fd); [zzangdol] remove - it is separate in HWInterface with camera_id

    if (m_cam_fd <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    if (m_flag_camera_start > 0) {
        LOG_TIME_START(0)
        ALOGW("WARN(%s):Camera was in preview, should have been stopped\n", __func__);
        stopPreview();
        LOG_TIME_END(0)
    }

    memset(&m_events_c, 0, sizeof(m_events_c));
    m_events_c.fd = m_cam_fd;
    m_events_c.events = POLLIN | POLLERR;

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
    if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUV420)
        ALOGV("SnapshotFormat:V4L2_PIX_FMT_YUV420");
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_NV12)
        ALOGV("SnapshotFormat:V4L2_PIX_FMT_NV12");
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_NV12T)
        ALOGV("SnapshotFormat:V4L2_PIX_FMT_NV12T");
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_NV21)
        ALOGV("SnapshotFormat:V4L2_PIX_FMT_NV21");
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUV422P)
        ALOGV("SnapshotFormat:V4L2_PIX_FMT_YUV422P");
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUYV)
        ALOGV("SnapshotFormat:V4L2_PIX_FMT_YUYV");
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_UYVY)
        ALOGV("SnapshotFormat:V4L2_PIX_FMT_UYVY");
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_RGB565)
        ALOGV("SnapshotFormat:V4L2_PIX_FMT_RGB565");
    else
        ALOGV("SnapshotFormat:UnknownFormat");
#endif

    LOG_TIME_START(1) // prepare
    int nframe = 1;

    ret = fimc_v4l2_enum_fmt(m_cam_fd,m_snapshot_v4lformat);
    CHECK(ret);
    ret = fimc_v4l2_s_fmt_cap(m_cam_fd, m_snapshot_width, m_snapshot_height, m_snapshot_v4lformat);
    CHECK(ret);
    ret = fimc_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, nframe);
    CHECK(ret);
    // Just use buffer #0
    ret = fimc_v4l2_querybuf(m_cam_fd, 0, m_capture_buf, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    CHECK(ret);

    ret = fimc_v4l2_qbuf(m_cam_fd, 0);
    CHECK(ret);

    ret = fimc_v4l2_streamon(m_cam_fd);
    CHECK(ret);
    LOG_TIME_END(1)

    LOG_TIME_START(2) // capture
    fimc_poll(&m_events_c);
    index = fimc_v4l2_dqbuf(m_cam_fd);
    fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_STREAM_PAUSE, 0);
    ALOGV("\nsnapshot dequeued buffer = %d snapshot_width = %d snapshot_height = %d\n\n",
            index, m_snapshot_width, m_snapshot_height);

    LOG_TIME_END(2)

    ALOGI("%s : calling memcpy from m_capture_buf", __func__);
    memcpy(yuv_buf, (unsigned char*)m_capture_buf[0].start, m_snapshot_width * m_snapshot_height * 2);
    LOG_TIME_START(5) // post
    fimc_v4l2_streamoff(m_cam_fd);
    LOG_TIME_END(5)

    LOG_CAMERA("getSnapshotAndJpeg intervals : stopPreview(%lu), prepare(%lu),"
                " capture(%lu), memcpy(%lu), yuv2Jpeg(%lu), post(%lu)  us",
                    LOG_TIME(0), LOG_TIME(1), LOG_TIME(2), LOG_TIME(3), LOG_TIME(4), LOG_TIME(5));
    /* JPEG encoding */
    JpegEncoder jpgEnc;
    int inFormat = JPG_MODESEL_YCBCR;
    int outFormat = JPG_422;

    switch (m_snapshot_v4lformat) {
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV12T:
    case V4L2_PIX_FMT_YUV420:
        outFormat = JPG_420;
        break;
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_YUV422P:
    default:
        outFormat = JPG_422;
        break;
    }

    if (jpgEnc.setConfig(JPEG_SET_ENCODE_IN_FORMAT, inFormat) != JPG_SUCCESS)
        ALOGE("[JPEG_SET_ENCODE_IN_FORMAT] Error\n");

    if (jpgEnc.setConfig(JPEG_SET_SAMPING_MODE, outFormat) != JPG_SUCCESS)
        ALOGE("[JPEG_SET_SAMPING_MODE] Error\n");

    image_quality_type_t jpegQuality;
    if (m_jpeg_quality >= 90)
        jpegQuality = JPG_QUALITY_LEVEL_1;
    else if (m_jpeg_quality >= 80)
        jpegQuality = JPG_QUALITY_LEVEL_2;
    else if (m_jpeg_quality >= 70)
        jpegQuality = JPG_QUALITY_LEVEL_3;
    else
        jpegQuality = JPG_QUALITY_LEVEL_4;

    if (jpgEnc.setConfig(JPEG_SET_ENCODE_QUALITY, jpegQuality) != JPG_SUCCESS)
        ALOGE("[JPEG_SET_ENCODE_QUALITY] Error\n");
    if (jpgEnc.setConfig(JPEG_SET_ENCODE_WIDTH, m_snapshot_width) != JPG_SUCCESS)
        ALOGE("[JPEG_SET_ENCODE_WIDTH] Error\n");

    if (jpgEnc.setConfig(JPEG_SET_ENCODE_HEIGHT, m_snapshot_height) != JPG_SUCCESS)
        ALOGE("[JPEG_SET_ENCODE_HEIGHT] Error\n");

    unsigned int snapshot_size = m_snapshot_width * m_snapshot_height * 2;
    unsigned char *pInBuf = (unsigned char *)jpgEnc.getInBuf(snapshot_size);

    if (pInBuf == NULL) {
        ALOGE("JPEG input buffer is NULL!!\n");
        return -1;
    }
    memcpy(pInBuf, yuv_buf, snapshot_size);

    setExifChangedAttribute();
    jpgEnc.encode(output_size, NULL);

    uint64_t outbuf_size;
    unsigned char *pOutBuf = (unsigned char *)jpgEnc.getOutBuf(&outbuf_size);

    if (pOutBuf == NULL) {
        ALOGE("JPEG output buffer is NULL!!\n");
        return -1;
    }

    memcpy(jpeg_buf, pOutBuf, outbuf_size);

    return 0;
}


int UVCCamera::setSnapshotSize(int width, int height)
{
    ALOGV("%s(width(%d), height(%d))", __func__, width, height);

    m_snapshot_width  = width;
    m_snapshot_height = height;

    return 0;
}

int UVCCamera::getSnapshotSize(int *width, int *height, int *frame_size)
{
    *width  = m_snapshot_width;
    *height = m_snapshot_height;

    int frame = 0;

    frame = m_frameSize(m_snapshot_v4lformat, m_snapshot_width, m_snapshot_height);

    // set it big.
    if (frame == 0)
        frame = m_snapshot_width * m_snapshot_height * BPP;

    *frame_size = frame;

    return 0;
}

int UVCCamera::getSnapshotMaxSize(int *width, int *height)
{
    switch (m_camera_id) {
    case CAMERA_ID_FRONT:
        m_snapshot_max_width  = MAX_FRONT_CAMERA_SNAPSHOT_WIDTH;
        m_snapshot_max_height = MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT;
        break;
    }

    *width  = m_snapshot_max_width;
    *height = m_snapshot_max_height;

    return 0;
}

int UVCCamera::setSnapshotPixelFormat(int pixel_format)
{
    int v4lpixelformat= pixel_format;

    if (m_snapshot_v4lformat != v4lpixelformat) {
        m_snapshot_v4lformat = v4lpixelformat;
    }

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
    if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUV420)
        ALOGE("%s : SnapshotFormat:V4L2_PIX_FMT_YUV420", __func__);
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_NV12)
        ALOGD("%s : SnapshotFormat:V4L2_PIX_FMT_NV12", __func__);
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_NV12T)
        ALOGD("%s : SnapshotFormat:V4L2_PIX_FMT_NV12T", __func__);
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_NV21)
        ALOGD("%s : SnapshotFormat:V4L2_PIX_FMT_NV21", __func__);
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUV422P)
        ALOGD("%s : SnapshotFormat:V4L2_PIX_FMT_YUV422P", __func__);
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUYV)
        ALOGD("%s : SnapshotFormat:V4L2_PIX_FMT_YUYV", __func__);
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_UYVY)
        ALOGD("%s : SnapshotFormat:V4L2_PIX_FMT_UYVY", __func__);
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_RGB565)
        ALOGD("%s : SnapshotFormat:V4L2_PIX_FMT_RGB565", __func__);
    else
        ALOGD("SnapshotFormat:UnknownFormat");
#endif
    return 0;
}

int UVCCamera::getSnapshotPixelFormat(void)
{
    return m_snapshot_v4lformat;
}

// ======================================================================
// Settings

int UVCCamera::getCameraId(void)
{
    return m_camera_id;
}


// -----------------------------------

int UVCCamera::setRotate(int angle)
{
    ALOGE("%s(angle(%d))", __func__, angle);

    if (m_angle != angle) {
        switch (angle) {
        case -360:
        case    0:
        case  360:
            m_angle = 0;
            break;

        case -270:
        case   90:
            m_angle = 90;
            break;

        case -180:
        case  180:
            m_angle = 180;
            break;

        case  -90:
        case  270:
            m_angle = 270;
            break;

        default:
            ALOGE("ERR(%s):Invalid angle(%d)", __func__, angle);
            return -1;
        }

        if (m_flag_camera_start) {
#if 0
            if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_ROTATION, angle) < 0) {
                ALOGE("ERR(%s):Fail on V4L2_CID_ROTATION", __func__);
                return -1;
            }
#endif
        }
    }

    return 0;
}

int UVCCamera::getRotate(void)
{
    ALOGV("%s : angle(%d)", __func__, m_angle);
    return m_angle;
}

int UVCCamera::setFrameRate(int frame_rate)
{
    ALOGV("%s(FrameRate(%d))", __func__, frame_rate);

    // not yet implemented

    return 0;
}

// -----------------------------------

int UVCCamera::setVerticalMirror(void)
{
    ALOGV("%s :", __func__);

    if (m_cam_fd <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_VFLIP, 0) < 0) {
        ALOGE("ERR(%s):Fail on V4L2_CID_VFLIP", __func__);
        return -1;
    }

    return 0;
}

int UVCCamera::setHorizontalMirror(void)
{
    ALOGV("%s :", __func__);

    if (m_cam_fd <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_HFLIP, 0) < 0) {
        ALOGE("ERR(%s):Fail on V4L2_CID_HFLIP", __func__);
        return -1;
    }

    return 0;
}


int UVCCamera::setGPSLatitude(const char *gps_latitude)
{
    ALOGV("%s(gps_latitude(%s))", __func__, gps_latitude);
    if (gps_latitude == NULL)
        m_gps_enabled = false;
    else {
        m_gps_enabled = true;
        m_gps_latitude = lround(strtod(gps_latitude, NULL) * 10000000);
    }

    ALOGV("%s(m_gps_latitude(%ld))", __func__, m_gps_latitude);
    return 0;
}

int UVCCamera::setGPSLongitude(const char *gps_longitude)
{
    ALOGV("%s(gps_longitude(%s))", __func__, gps_longitude);
    if (gps_longitude == NULL)
        m_gps_enabled = false;
    else {
        m_gps_enabled = true;
        m_gps_longitude = lround(strtod(gps_longitude, NULL) * 10000000);
    }

    ALOGV("%s(m_gps_longitude(%ld))", __func__, m_gps_longitude);
    return 0;
}

int UVCCamera::setGPSAltitude(const char *gps_altitude)
{
    ALOGV("%s(gps_altitude(%s))", __func__, gps_altitude);
    if (gps_altitude == NULL)
        m_gps_altitude = 0;
    else {
        m_gps_altitude = lround(strtod(gps_altitude, NULL) * 100);
    }

    ALOGV("%s(m_gps_altitude(%ld))", __func__, m_gps_altitude);
    return 0;
}

int UVCCamera::setGPSTimeStamp(const char *gps_timestamp)
{
    ALOGV("%s(gps_timestamp(%s))", __func__, gps_timestamp);
    if (gps_timestamp == NULL)
        m_gps_timestamp = 0;
    else
        m_gps_timestamp = atol(gps_timestamp);

    ALOGV("%s(m_gps_timestamp(%ld))", __func__, m_gps_timestamp);
    return 0;
}

int UVCCamera::setGPSProcessingMethod(const char *gps_processing_method)
{
    ALOGV("%s(gps_processing_method(%s))", __func__, gps_processing_method);
    memset(mExifInfo.gps_processing_method, 0, sizeof(mExifInfo.gps_processing_method));
    if (gps_processing_method != NULL) {
        size_t len = strlen(gps_processing_method);
        if (len > sizeof(mExifInfo.gps_processing_method)) {
            len = sizeof(mExifInfo.gps_processing_method);
        }
        memcpy(mExifInfo.gps_processing_method, gps_processing_method, len);
    }
    return 0;
}


//======================================================================

int UVCCamera::setRecordingSize(int width, int height)
{
     ALOGV("%s(width(%d), height(%d))", __func__, width, height);

     m_recording_width  = width;
     m_recording_height = height;

     return 0;
}

//======================================================================

int UVCCamera::setExifOrientationInfo(int orientationInfo)
{
     ALOGV("%s(orientationInfo(%d))", __func__, orientationInfo);

     if (orientationInfo < 0) {
         ALOGE("ERR(%s):Invalid orientationInfo (%d)", __func__, orientationInfo);
         return -1;
     }
     m_exif_orientation = orientationInfo;

     return 0;
}


const __u8* UVCCamera::getCameraSensorName(void)
{
    ALOGV("%s", __func__);

    return fimc_v4l2_enuminput(m_cam_fd, getCameraId());
}

// ======================================================================
// Jpeg

int UVCCamera::setJpegThumbnailSize(int width, int height)
{
    ALOGV("%s(width(%d), height(%d))", __func__, width, height);

    m_jpeg_thumbnail_width  = width;
    m_jpeg_thumbnail_height = height;

    return 0;
}

int UVCCamera::getJpegThumbnailSize(int *width, int  *height)
{
    if (width)
        *width   = m_jpeg_thumbnail_width;
    if (height)
        *height  = m_jpeg_thumbnail_height;

    return 0;
}

void UVCCamera::setExifFixedAttribute()
{
    char property[PROPERTY_VALUE_MAX];

    //2 0th IFD TIFF Tags
    //3 Maker
    property_get("ro.product.brand", property, EXIF_DEF_MAKER);
    strncpy((char *)mExifInfo.maker, property,
                sizeof(mExifInfo.maker) - 1);
    mExifInfo.maker[sizeof(mExifInfo.maker) - 1] = '\0';
    //3 Model
    property_get("ro.product.model", property, EXIF_DEF_MODEL);
    strncpy((char *)mExifInfo.model, property,
                sizeof(mExifInfo.model) - 1);
    mExifInfo.model[sizeof(mExifInfo.model) - 1] = '\0';
    //3 Software
    property_get("ro.build.id", property, EXIF_DEF_SOFTWARE);
    strncpy((char *)mExifInfo.software, property,
                sizeof(mExifInfo.software) - 1);
    mExifInfo.software[sizeof(mExifInfo.software) - 1] = '\0';

    //3 YCbCr Positioning
    mExifInfo.ycbcr_positioning = EXIF_DEF_YCBCR_POSITIONING;

    //2 0th IFD Exif Private Tags
    //3 F Number
    mExifInfo.fnumber.num = EXIF_DEF_FNUMBER_NUM;
    mExifInfo.fnumber.den = EXIF_DEF_FNUMBER_DEN;
    //3 Exposure Program
    mExifInfo.exposure_program = EXIF_DEF_EXPOSURE_PROGRAM;
    //3 Exif Version
    memcpy(mExifInfo.exif_version, EXIF_DEF_EXIF_VERSION, sizeof(mExifInfo.exif_version));
    //3 Aperture
    uint32_t av = APEX_FNUM_TO_APERTURE((double)mExifInfo.fnumber.num/mExifInfo.fnumber.den);
    mExifInfo.aperture.num = av*EXIF_DEF_APEX_DEN;
    mExifInfo.aperture.den = EXIF_DEF_APEX_DEN;
    //3 Maximum lens aperture
    mExifInfo.max_aperture.num = mExifInfo.aperture.num;
    mExifInfo.max_aperture.den = mExifInfo.aperture.den;
    //3 Lens Focal Length
    mExifInfo.focal_length.num = FRONT_CAMERA_FOCAL_LENGTH;

    mExifInfo.focal_length.den = EXIF_DEF_FOCAL_LEN_DEN;
    //3 User Comments
    strcpy((char *)mExifInfo.user_comment, EXIF_DEF_USERCOMMENTS);
    //3 Color Space information
    mExifInfo.color_space = EXIF_DEF_COLOR_SPACE;
    //3 Exposure Mode
    mExifInfo.exposure_mode = EXIF_DEF_EXPOSURE_MODE;

    //2 0th IFD GPS Info Tags
    unsigned char gps_version[4] = { 0x02, 0x02, 0x00, 0x00 };
    memcpy(mExifInfo.gps_version_id, gps_version, sizeof(gps_version));

    //2 1th IFD TIFF Tags
    mExifInfo.compression_scheme = EXIF_DEF_COMPRESSION;
    mExifInfo.x_resolution.num = EXIF_DEF_RESOLUTION_NUM;
    mExifInfo.x_resolution.den = EXIF_DEF_RESOLUTION_DEN;
    mExifInfo.y_resolution.num = EXIF_DEF_RESOLUTION_NUM;
    mExifInfo.y_resolution.den = EXIF_DEF_RESOLUTION_DEN;
    mExifInfo.resolution_unit = EXIF_DEF_RESOLUTION_UNIT;
}

void UVCCamera::setExifChangedAttribute()
{
    //2 0th IFD TIFF Tags
    //3 Width
    mExifInfo.width = m_snapshot_width;
    //3 Height
    mExifInfo.height = m_snapshot_height;
    //3 Orientation
    switch (m_exif_orientation) {
    case 0:
        mExifInfo.orientation = EXIF_ORIENTATION_UP;
        break;
    case 90:
        mExifInfo.orientation = EXIF_ORIENTATION_90;
        break;
    case 180:
        mExifInfo.orientation = EXIF_ORIENTATION_180;
        break;
    case 270:
        mExifInfo.orientation = EXIF_ORIENTATION_270;
        break;
    default:
        mExifInfo.orientation = EXIF_ORIENTATION_UP;
        break;
    }
    //3 Date time
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime((char *)mExifInfo.date_time, 20, "%Y:%m:%d %H:%M:%S", timeinfo);

    //2 0th IFD Exif Private Tags
    //3 Exposure Time
    int shutterSpeed = 100;
    mExifInfo.exposure_time.num = 1;
    // x us -> 1/x s */
    mExifInfo.exposure_time.den = (uint32_t)(1000000 / shutterSpeed);

    // FIXME - probably not correct
    mExifInfo.iso_speed_rating = 100;

    uint32_t av, tv, bv, sv, ev;
    av = APEX_FNUM_TO_APERTURE((double)mExifInfo.fnumber.num / mExifInfo.fnumber.den);
    tv = APEX_EXPOSURE_TO_SHUTTER((double)mExifInfo.exposure_time.num / mExifInfo.exposure_time.den);
    sv = APEX_ISO_TO_FILMSENSITIVITY(mExifInfo.iso_speed_rating);
    bv = av + tv - sv;
    ev = av + tv;
    ALOGD("Shutter speed=%d us, iso=%d\n", shutterSpeed, mExifInfo.iso_speed_rating);
    ALOGD("AV=%d, TV=%d, SV=%d\n", av, tv, sv);

    //3 Shutter Speed
    mExifInfo.shutter_speed.num = tv*EXIF_DEF_APEX_DEN;
    mExifInfo.shutter_speed.den = EXIF_DEF_APEX_DEN;
    //3 Brightness
    mExifInfo.brightness.num = bv*EXIF_DEF_APEX_DEN;
    mExifInfo.brightness.den = EXIF_DEF_APEX_DEN;
    //3 Exposure Bias
    mExifInfo.exposure_bias.num = 0;
    mExifInfo.exposure_bias.den = 0;

    //3 Metering Mode
    mExifInfo.metering_mode = EXIF_METERING_AVERAGE;

    //3 Flash
    mExifInfo.flash = EXIF_DEF_FLASH;

    //3 White Balance
    mExifInfo.white_balance = EXIF_WB_AUTO;

    //3 Scene Capture Type
    mExifInfo.scene_capture_type = EXIF_SCENE_STANDARD;

    //2 0th IFD GPS Info Tags
    if (m_gps_enabled) {
        if (m_gps_latitude >= 0)
            strcpy((char *)mExifInfo.gps_latitude_ref, "N");
        else
            strcpy((char *)mExifInfo.gps_latitude_ref, "S");

        if (m_gps_longitude >= 0)
            strcpy((char *)mExifInfo.gps_longitude_ref, "E");
        else
            strcpy((char *)mExifInfo.gps_longitude_ref, "W");

        if (m_gps_altitude >= 0)
            mExifInfo.gps_altitude_ref = 0;
        else
            mExifInfo.gps_altitude_ref = 1;

        mExifInfo.gps_latitude[0].num = (uint32_t)labs(m_gps_latitude);
        mExifInfo.gps_latitude[0].den = 10000000;
        mExifInfo.gps_latitude[1].num = 0;
        mExifInfo.gps_latitude[1].den = 1;
        mExifInfo.gps_latitude[2].num = 0;
        mExifInfo.gps_latitude[2].den = 1;

        mExifInfo.gps_longitude[0].num = (uint32_t)labs(m_gps_longitude);
        mExifInfo.gps_longitude[0].den = 10000000;
        mExifInfo.gps_longitude[1].num = 0;
        mExifInfo.gps_longitude[1].den = 1;
        mExifInfo.gps_longitude[2].num = 0;
        mExifInfo.gps_longitude[2].den = 1;

        mExifInfo.gps_altitude.num = (uint32_t)labs(m_gps_altitude);
        mExifInfo.gps_altitude.den = 100;

        struct tm tm_data;
        gmtime_r(&m_gps_timestamp, &tm_data);
        mExifInfo.gps_timestamp[0].num = tm_data.tm_hour;
        mExifInfo.gps_timestamp[0].den = 1;
        mExifInfo.gps_timestamp[1].num = tm_data.tm_min;
        mExifInfo.gps_timestamp[1].den = 1;
        mExifInfo.gps_timestamp[2].num = tm_data.tm_sec;
        mExifInfo.gps_timestamp[2].den = 1;
        snprintf((char*)mExifInfo.gps_datestamp, sizeof(mExifInfo.gps_datestamp),
                "%04d:%02d:%02d", tm_data.tm_year + 1900, tm_data.tm_mon + 1, tm_data.tm_mday);

        mExifInfo.enableGps = true;
    } else {
        mExifInfo.enableGps = false;
    }

    //2 1th IFD TIFF Tags
    mExifInfo.widthThumb = m_jpeg_thumbnail_width;
    mExifInfo.heightThumb = m_jpeg_thumbnail_height;
}

// ======================================================================
// Conversions

inline int UVCCamera::m_frameSize(int format, int width, int height)
{
    int size = 0;

    switch (format) {
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case v4l2_fourcc('M','4','2','0'): // YUV420 known as M420
        size = (width * height * 3 / 2);
        break;

    case V4L2_PIX_FMT_NV12T:
        size = ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)) +
                            ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height / 2));
        break;

    case V4L2_PIX_FMT_YUV422P:
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_UYVY:
        size = (width * height * 2);
        break;

    default :
        ALOGE("ERR(%s):Invalid V4L2 pixel format(%d)\n", __func__, format);
    case V4L2_PIX_FMT_RGB565:
        size = (width * height * BPP);
        break;
    }

    return size;
}

status_t UVCCamera::dump(int fd)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    snprintf(buffer, 255, "dump(%d)\n", fd);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

double UVCCamera::jpeg_ratio = 0.7;
int UVCCamera::interleaveDataSize = 5242880;
int UVCCamera::jpegLineLength = 636;

}; // namespace android
