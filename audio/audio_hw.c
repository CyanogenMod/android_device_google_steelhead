/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2011 Texas Instruments Inc.
 * Copyright (C) 2013 Howard M. Harte (hharte@magicandroidapps.com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#define LOG_TAG "audio_hw_steelhead"
/*#define LOG_NDEBUG 0*/
//#define LOG_NDEBUG_FUNCTION
#ifndef LOG_NDEBUG_FUNCTION
#define LOGFUNC(...) ((void)0)
#else
#define LOGFUNC(...) (ALOGE(__VA_ARGS__))
#endif

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include <fcntl.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_ 1
#include <sys/_system_properties.h>
#include <kiss_fft.h>

/* ALSA cards for Tungsten */
typedef enum supported_cards {
    CARD_STEELHEAD_HDMI = 0,
    CARD_STEELHEAD_SPDIF,
    CARD_STEELHEAD_TAS5713,
    CARD_STEELHEAD_COUNT
} supported_cards_t;

/* ALSA ports for Tungsten */
#define PORT_MM 0

#define TAS5713_GET_MASTER_VOLUME 0x800141f8
#define TAS5713_SET_MASTER_VOLUME 0x400141f9

#define STEELHEAD_HDMI_ENABLED_PROP	"persist.sys.audio.hdmi_enabled"
#define STEELHEAD_SPDIF_ENABLED_PROP	"persist.sys.audio.spdif_enabled"
#define STEELHEAD_TAS5713_ENABLED_PROP	"persist.sys.audio.tas5713_enabled"
#define STEELHEAD_HDMI_ACTIVE_PROP	"sys.audio.hdmi"

#define ABE_BASE_FRAME_COUNT 24
/* number of base blocks in a short period (low latency) */
#define SHORT_PERIOD_MULTIPLIER 80  /* 40 ms */
/* number of frames per short period (low latency) */
#define SHORT_PERIOD_SIZE (ABE_BASE_FRAME_COUNT * SHORT_PERIOD_MULTIPLIER)
/* number of short periods in a long period (low power) */
#define LONG_PERIOD_MULTIPLIER 1  /* 40 ms */
/* number of frames per long period (low power) */
#define LONG_PERIOD_SIZE (SHORT_PERIOD_SIZE * LONG_PERIOD_MULTIPLIER)
/* number of periods for playback */
#define PLAYBACK_PERIOD_COUNT 4
/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000

#define RESAMPLER_BUFFER_FRAMES (SHORT_PERIOD_SIZE * 2)
#define RESAMPLER_BUFFER_SIZE (4 * RESAMPLER_BUFFER_FRAMES)

#define DEFAULT_OUT_SAMPLING_RATE 48000

/* product-specific defines */
#define PRODUCT_DEVICE_PROPERTY "ro.product.device"
#define PRODUCT_DEVICE_STEELHEAD "steelhead"

enum supported_boards {
    STEELHEAD
};

#define PCM_ERROR_MAX 128

struct pcm {
    int fd;
    unsigned int flags;
    int running:1;
    int underruns;
    unsigned int buffer_size;
    unsigned int boundary;
    char error[PCM_ERROR_MAX];
    struct pcm_config config;
    struct snd_pcm_mmap_status *mmap_status;
    struct snd_pcm_mmap_control *mmap_control;
    struct snd_pcm_sync_ptr *sync_ptr;
    void *mmap_buffer;
    unsigned int noirq_frames_per_msec;
    int wait_for_avail_min;
};

struct pcm_config pcm_config_mm = {
    .channels = 2,
    .rate = DEFAULT_OUT_SAMPLING_RATE,
    .period_size = LONG_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

struct tungsten_audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    int mode;
    int devices;
    float master_volume;
    bool master_mute;
    float voice_volume;
    int board_type;
    int tas5713_fd;
};

struct tungsten_stream_out {
    struct audio_stream_out stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm[CARD_STEELHEAD_COUNT];
    struct resampler_itfe *resampler;
    char *buffer, *volbuffer;
    int standby;
    int write_threshold;
    unsigned int stamp;

    struct tungsten_audio_device *dev;
};

/**
 * NOTE: when multiple mutexes have to be acquired, always respect the following order:
 *        hw device > in stream > out stream
 */

static int tas5713_connected(void);
static int tas5713_set_volume(int fd, float volume);
static int adev_set_voice_volume(struct audio_hw_device *dev, float volume);
static int do_output_standby(struct tungsten_stream_out *out);
static int adev_set_master_volume(struct audio_hw_device *dev, float volume);

/**/
/* Output configuration changes */

static const char * const prop_names[CARD_STEELHEAD_COUNT] = {
	STEELHEAD_HDMI_ENABLED_PROP,
	STEELHEAD_SPDIF_ENABLED_PROP,
	STEELHEAD_TAS5713_ENABLED_PROP,
};
static const prop_info *prop_watch[CARD_STEELHEAD_COUNT];
static unsigned int prop_serials[CARD_STEELHEAD_COUNT];
static int prop_enabled[CARD_STEELHEAD_COUNT];
static volatile unsigned int prop_stamp;

static void *prop_watch_thread(void *arg)
{
    int i;
    char value[PROP_VALUE_MAX];
    unsigned int serial = 0;

    for (i = 0; i < CARD_STEELHEAD_COUNT; ++i)
        prop_enabled[i] = 1;

    for (i = 0; i < CARD_STEELHEAD_COUNT; ++i) {
        prop_watch[i] = __system_property_find(prop_names[i]);
        if (!prop_watch[i]) {
            __system_property_add(prop_names[i], strlen(prop_names[i]), "1", 1);
            prop_watch[i] = __system_property_find(prop_names[i]);
            if (!prop_watch[i])
                return NULL;
        }
        prop_serials[i] = __system_property_serial(prop_watch[i]);
        __system_property_read(prop_watch[i], 0, value);
        prop_enabled[i] = (value[0] == '1');
    }

    while (1) {
        serial = __system_property_wait_any(serial);
        for (i = 0; i < CARD_STEELHEAD_COUNT; ++i) {
            unsigned int prop_serial = __system_property_serial(prop_watch[i]);
            if (prop_serials[i] == prop_serial) continue;
            __system_property_read(prop_watch[i], 0, value);
            prop_enabled[i] = (value[0] == '1');
            prop_serials[i] = prop_serial;
            ++prop_stamp;
        }
    }

    /* not reached */
    return NULL;
}

/**/

static int get_boardtype(struct tungsten_audio_device *adev)
{
    char board[PROPERTY_VALUE_MAX];
    int status = 0;
    int board_type = 0;

    LOGFUNC("%s(%p)", __FUNCTION__, adev);

    property_get(PRODUCT_DEVICE_PROPERTY, board, PRODUCT_DEVICE_STEELHEAD);
    /* return true if the property matches the given value */
    if(!strcmp(board, PRODUCT_DEVICE_STEELHEAD)) {
        adev->board_type = STEELHEAD;
    }
    else
        return -EINVAL;

    return 0;
}
/* The enable flag when 0 makes the assumption that enums are disabled by
 * "Off" and integers/booleans by 0 */


static void select_mode(struct tungsten_audio_device *adev)
{
    LOGFUNC("%s(%p)", __FUNCTION__, adev);
}

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct tungsten_stream_out *out)
{
    struct tungsten_audio_device *adev = out->dev;
    unsigned int port = PORT_MM;
    int i;

    LOGFUNC("%s(%p) devices=%x", __FUNCTION__, adev, adev->devices);

    out->config.channels = 2;
    out->config.rate = DEFAULT_OUT_SAMPLING_RATE;

    out->write_threshold = PLAYBACK_PERIOD_COUNT * LONG_PERIOD_SIZE;
    out->config.start_threshold = SHORT_PERIOD_SIZE * 2;
    out->config.avail_min = LONG_PERIOD_SIZE;

    out->stamp = prop_stamp;
    for (i = 0; i < CARD_STEELHEAD_COUNT; ++i) {
        if (!prop_enabled[i] || (i==CARD_STEELHEAD_TAS5713 && !tas5713_connected())) {
            out->pcm[i] = NULL;
            continue;
        }

        out->pcm[i] = pcm_open(i, PORT_MM, PCM_OUT | PCM_MMAP, &out->config);
        if (!pcm_is_ready(out->pcm[i])) {
            ALOGE("cannot open pcm_out driver: %s", pcm_get_error(out->pcm[i]));
            pcm_close(out->pcm[i]);
            out->pcm[i] = NULL;
        }
    }
    adev->tas5713_fd = out->pcm[CARD_STEELHEAD_TAS5713] ? out->pcm[CARD_STEELHEAD_TAS5713]->fd : -1;
    if (adev->tas5713_fd >= 0) {
        tas5713_set_volume(adev->tas5713_fd, adev->master_mute ? 0.0f : adev->master_volume);
    }

    if (out->resampler)
        out->resampler->reset(out->resampler);

    return 0;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct tungsten_stream_out *out = (struct tungsten_stream_out *)stream;
    uint32_t sample_rate;

    LOGFUNC("%s(%p)", __FUNCTION__, stream);

    pthread_mutex_lock(&out->lock);
    sample_rate = out->config.rate;
    pthread_mutex_unlock(&out->lock);
    
    return sample_rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    LOGFUNC("%s(%p, %d)", __FUNCTION__, stream, rate);

    if (rate != DEFAULT_OUT_SAMPLING_RATE)
        return -EINVAL;

    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct tungsten_stream_out *out = (struct tungsten_stream_out *)stream;

    LOGFUNC("%s(%p)", __FUNCTION__, stream);

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size_t size = (SHORT_PERIOD_SIZE * DEFAULT_OUT_SAMPLING_RATE) / out->config.rate;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_frame_size((struct audio_stream *)stream);
}

static uint32_t out_get_channels(const struct audio_stream *stream)
{
    LOGFUNC("%s(%p)", __FUNCTION__, stream);

    return AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    LOGFUNC("%s(%p)", __FUNCTION__, stream);

    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    LOGFUNC("%s(%p)", __FUNCTION__, stream);
    if (format != AUDIO_FORMAT_PCM_16_BIT)
        return -EINVAL;
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct tungsten_stream_out *out)
{
    struct tungsten_audio_device *adev = out->dev;
    int i;

    LOGFUNC("%s(%p)", __FUNCTION__, out);

    if (out->standby)
        return 0;

    adev->tas5713_fd = -1;
    for (i = 0; i < CARD_STEELHEAD_COUNT; ++i) {
        if (!out->pcm[i]) continue;
        pcm_close(out->pcm[i]);
        out->pcm[i] = NULL;
    }

    out->standby = 1;

    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct tungsten_stream_out *out = (struct tungsten_stream_out *)stream;
    int status;

    LOGFUNC("%s(%p)", __FUNCTION__, stream);

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    status = do_output_standby(out);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);
    return status;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    LOGFUNC("%s(%p, %d)", __FUNCTION__, stream, fd);

    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct tungsten_stream_out *out = (struct tungsten_stream_out *)stream;
    struct tungsten_audio_device *adev = out->dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret, val = 0;

    LOGFUNC("%s(%p, %s)", __FUNCTION__, stream, kvpairs);

    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        /**/
    }

    str_parms_destroy(parms);
    return ret;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    LOGFUNC("%s(%p, %s)", __FUNCTION__, stream, keys);

    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct tungsten_stream_out *out = (struct tungsten_stream_out *)stream;

    LOGFUNC("%s(%p)", __FUNCTION__, stream);
    return (SHORT_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT * 1000) / out->config.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    LOGFUNC("%s(%p, lvol=%f, rvol=%f)", __FUNCTION__, stream, left, right);
    ALOGE("%s(%p, lvol=%f, rvol=%f)", __FUNCTION__, stream, left, right);

    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret;
    struct tungsten_stream_out *out = (struct tungsten_stream_out *)stream;
    struct tungsten_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_frame_size(&out->stream.common);
    size_t in_frames = bytes / frame_size;
    size_t out_frames = RESAMPLER_BUFFER_SIZE / frame_size;
    int kernel_frames;
    void *buf = NULL, *volbuf = NULL;
    struct pcm *sync_pcm = NULL;
    float master_volume;
    int i;

    LOGFUNC("%s(%p, %p, %d)", __FUNCTION__, stream, buffer, bytes);

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0) {
            pthread_mutex_unlock(&out->lock);
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        out->standby = 0;
    }

    while (out->stamp != prop_stamp) {
        out->stamp = prop_stamp;

        for (i = 0; i < CARD_STEELHEAD_COUNT; ++i) {
            if (prop_enabled[i] == !!out->pcm[i])
                continue;
            if (out->pcm[i]) {
                pcm_close(out->pcm[i]);
                out->pcm[i] = NULL;
            } else {
                if (i==CARD_STEELHEAD_TAS5713 && !tas5713_connected())
                    continue;
                out->pcm[i] = pcm_open(i, PORT_MM, PCM_OUT | PCM_MMAP, &out->config);
                if (!pcm_is_ready(out->pcm[i])) {
                    ALOGE("cannot open pcm_out driver: %s", pcm_get_error(out->pcm[i]));
                    pcm_close(out->pcm[i]);
                    out->pcm[i] = NULL;
                }
            }
        }

        adev->tas5713_fd = out->pcm[CARD_STEELHEAD_TAS5713] ? out->pcm[CARD_STEELHEAD_TAS5713]->fd : -1;
        if (adev->tas5713_fd >= 0) {
            tas5713_set_volume(adev->tas5713_fd, adev->master_mute ? 0.0f : adev->master_volume);
        }
    }

    master_volume = adev->master_mute ? 0.0f : adev->master_volume;
    pthread_mutex_unlock(&adev->lock);

    /* only use resampler if required */
    if (out->config.rate != DEFAULT_OUT_SAMPLING_RATE) {
#if 0
        if (out->rate_changed) {
            release_resampler(out->resampler);
            out->resampler = NULL;
        }
#endif

        if (!out->resampler) {
            ret = create_resampler(out->config.rate,
                    DEFAULT_OUT_SAMPLING_RATE,
                    out->config.channels,
                    RESAMPLER_QUALITY_DEFAULT,
                    NULL,
                    &out->resampler);
            if (ret != 0)
                goto exit;
            out->buffer = realloc(out->buffer, RESAMPLER_BUFFER_SIZE);
        }

        out->resampler->resample_from_input(out->resampler,
                (int16_t *)buffer,
                &in_frames,
                (int16_t *)out->buffer,
                &out_frames);

        buf = out->buffer;
    } else {
        out_frames = in_frames;
        buf = (void *)buffer;
    }

    if (master_volume < 1.0f && (out->pcm[CARD_STEELHEAD_HDMI] || out->pcm[CARD_STEELHEAD_SPDIF])) {
        if (!out->volbuffer)
            out->volbuffer = malloc(RESAMPLER_BUFFER_SIZE);

        /* todo: support left/right volume */
        uint32_t mixl = (1<<16) * master_volume;
        uint32_t mixr = (1<<16) * master_volume;
        int16_t *out16 = (int16_t*)out->volbuffer, *in16 = (int16_t*)buf;

        for (i = 0; (unsigned)i < out_frames; ++i) {
                *out16++ = (mixl * *in16++) >> 16;
                *out16++ = (mixr * *in16++) >> 16;
        }

        volbuf = out->volbuffer;
    } else {
        volbuf = buf;
    }

    /* do not allow more than out->write_threshold frames in kernel pcm driver buffer */
    /* steelhead derives all clocks from sys_clkin_ck, so hopefully they match. */
    for (i = CARD_STEELHEAD_COUNT; i--; ) {
        if (!out->pcm[i]) continue;
        sync_pcm = out->pcm[i];
        break;
    }

    if (sync_pcm) do {
        struct timespec time_stamp;

        if (pcm_get_htimestamp(sync_pcm, (unsigned int *)&kernel_frames, &time_stamp) < 0)
            break;

        kernel_frames = pcm_get_buffer_size(sync_pcm) - kernel_frames;
        if (kernel_frames > out->write_threshold) {
            unsigned long time = (unsigned long)
                    (((int64_t)(kernel_frames - out->write_threshold) * 1000000) /
                            DEFAULT_OUT_SAMPLING_RATE);
            if (time < MIN_WRITE_SLEEP_US)
                time = MIN_WRITE_SLEEP_US;
            usleep(time);
        }
    } while (kernel_frames > out->write_threshold);

    for (i = 0; i < CARD_STEELHEAD_COUNT; ++i) {
        if (!out->pcm[i])
            continue;

        if (i == CARD_STEELHEAD_TAS5713) {
            ret = pcm_mmap_write(out->pcm[i], (void *)buf, out_frames * frame_size);
        } else {
            ret = pcm_mmap_write(out->pcm[i], (void *)volbuf, out_frames * frame_size);
        }
        if (ret < 0)
            break;
    }

exit:
    pthread_mutex_unlock(&out->lock);

    if (ret != 0) {
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               out_get_sample_rate(&stream->common));
    }

    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    LOGFUNC("%s(%p, %p)", __FUNCTION__, stream, dsp_frames);

    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    LOGFUNC("%s(%p, %p)", __FUNCTION__, stream, effect);

    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    LOGFUNC("%s(%p, %p)", __FUNCTION__, stream, effect);

    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
    struct tungsten_audio_device *ladev = (struct tungsten_audio_device *)dev;
    struct tungsten_stream_out *out;
    int ret;

    LOGFUNC("%s(%p, 0x%04x, %p)", __FUNCTION__, dev, devices,
                        stream_out);

    out = (struct tungsten_stream_out *)calloc(1, sizeof(struct tungsten_stream_out));
    ALOGV("%s %d\n", __func__, __LINE__);
    if (!out)
        return -ENOMEM;

    ALOGV("%s %d\n", __func__, __LINE__);
    out->resampler = NULL;

    ALOGE("%s %d\n", __func__, __LINE__);
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;

    out->config = pcm_config_mm;

    out->dev = ladev;
    out->standby = 1;

    ALOGV("%s %d\n", __func__, __LINE__);
    config->format = out_get_format(&out->stream.common);
    ALOGV("%s %d\n", __func__, __LINE__);
    config->channel_mask = out_get_channels(&out->stream.common);
    ALOGV("%s %d\n", __func__, __LINE__);
    config->sample_rate = out_get_sample_rate(&out->stream.common);
    ALOGV("%s %d\n", __func__, __LINE__);

    *stream_out = &out->stream;
    ALOGV("%s %d\n", __func__, __LINE__);

    return 0;

err_open:
    ALOGV("%s %d\n", __func__, __LINE__);
    free(out);
    ALOGV("%s %d\n", __func__, __LINE__);
    *stream_out = NULL;
    ALOGV("%s %d\n", __func__, __LINE__);
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct tungsten_stream_out *out = (struct tungsten_stream_out *)stream;
    struct tungsten_audio_device *adev = out->dev;
    int i;

    LOGFUNC("%s(%p, %p)", __FUNCTION__, dev, stream);

    adev->tas5713_fd = -1;
    for (i = 0; i < CARD_STEELHEAD_COUNT; ++i) {
        if (!out->pcm[i]) continue;
        pcm_close(out->pcm[i]);
        out->pcm[i] = NULL;
    }

    out_standby(&stream->common);
    if (out->volbuffer)
        free(out->volbuffer);
    if (out->buffer)
        free(out->buffer);
    if (out->resampler)
        release_resampler(out->resampler);

    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct tungsten_audio_device *adev = (struct tungsten_audio_device *)dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret;

    LOGFUNC("%s(%p, %s)", __FUNCTION__, dev, kvpairs);

    parms = str_parms_create_str(kvpairs);

    ret = 0;

    return ret;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    LOGFUNC("%s(%p, %s)", __FUNCTION__, dev, keys);

    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    LOGFUNC("%s(%p)", __FUNCTION__, dev);
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct tungsten_audio_device *adev = (struct tungsten_audio_device *)dev;

    LOGFUNC("%s(%p, %f)", __FUNCTION__, dev, volume);
    ALOGE("%s(%p, %f)--", __func__, dev, volume);
    adev->voice_volume = volume;

    return 0;
}

static int adev_set_master_mute(struct audio_hw_device *dev, bool mute)
{
    struct tungsten_audio_device *adev = (struct tungsten_audio_device *)dev;
    float previous_volume = adev->master_volume;
    int ret;

    ALOGE("%s(%p, %d)--", __func__, dev, mute);

    adev->master_mute = mute;

    ret = adev_set_master_volume(dev, mute ? 0.0f : adev->master_volume);
    if (ret) {
        ALOGE("%s: failed to %s.", __func__, (mute ? "mute" : "unmute"));
    }

    adev->master_volume = previous_volume;

    return 0;
}

static int adev_get_master_mute(struct audio_hw_device *dev, bool *mute)
{
    struct tungsten_audio_device *adev = (struct tungsten_audio_device *)dev;
    *mute = adev->master_mute;

    ALOGE("%s(%p, %d)--", __func__, dev, *mute);

    return 0;
}

static int tas5713_connected(void)
{
    int fd = open("/sys/class/switch/hdmi_audio/state", O_RDONLY);
    char buf[3] = {'0','\n','\0'};
    if (fd < 0) return 0;
    read(fd, buf, 1);
    close(fd);
    return (buf[0] == '1');
}

static int tas5713_set_volume(int fd, float volume)
{
    unsigned char regvol = 0xff;
    int ret;

    if (volume > 0.0f) {
        /* Convert volume setting to register value for TAS5713. */
        regvol = (unsigned char)(0xAA - (127 * volume));
    }

    LOGFUNC("Volume %f=regval 0x%02x", volume, regvol);

    if ((ret = ioctl(fd, TAS5713_SET_MASTER_VOLUME, &regvol)) < 0) {
        ALOGE("Tungsten audio hardware unable to set volume, result was %d\n", ret);
        return -ENOSYS;
    }
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    struct tungsten_audio_device *adev = (struct tungsten_audio_device *)dev;

    LOGFUNC("%s(%p, volume=%f)", __FUNCTION__, dev, volume);

    pthread_mutex_lock(&adev->lock);
    if (adev->tas5713_fd >= 0) {
        tas5713_set_volume(adev->tas5713_fd, volume);
    }
    adev->master_volume = volume;
    pthread_mutex_unlock(&adev->lock);

    ALOGE("%s(%p, %f)--", __func__, dev, volume);

    return 0;    /* If any value other than 0 is returned,
                  * the software mixer will emulate this capability. */
}

static int adev_set_mode(struct audio_hw_device *dev, int mode)
{
    struct tungsten_audio_device *adev = (struct tungsten_audio_device *)dev;

    LOGFUNC("%s(%p, %d)", __FUNCTION__, dev, mode);

    pthread_mutex_lock(&adev->lock);
    if (adev->mode != mode) {
        adev->mode = mode;
        select_mode(adev);
    }
    pthread_mutex_unlock(&adev->lock);

    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct tungsten_audio_device *adev = (struct tungsten_audio_device *)dev;

    LOGFUNC("%s(%p, %d)", __FUNCTION__, dev, state);

    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct tungsten_audio_device *adev = (struct tungsten_audio_device *)dev;

    LOGFUNC("%s(%p, %p)", __FUNCTION__, dev, state);

    *state = true;

    return 0;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    LOGFUNC("%s(%p, %d)", __FUNCTION__, device, fd);

    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct tungsten_audio_device *adev = (struct tungsten_audio_device *)device;

    LOGFUNC("%s(%p)", __FUNCTION__, device);

    free(device);
    return 0;
}

static uint32_t adev_get_supported_devices(const struct audio_hw_device *dev)
{
    LOGFUNC("%s(%p)", __FUNCTION__, dev);

    return (/* OUT */
            AUDIO_DEVICE_OUT_AUX_DIGITAL |
            AUDIO_DEVICE_OUT_SPEAKER |
            AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET |
            AUDIO_DEVICE_OUT_DEFAULT);
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct tungsten_audio_device *adev;
    int ret, i;
    pthread_mutexattr_t mta;
    unsigned int num_ctls;
    unsigned char regvol=0x50;

    LOGFUNC("%s(%p, %s, %p)", __FUNCTION__, module, name, device);

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = calloc(1, sizeof(struct tungsten_audio_device));
    if (!adev)
        return -ENOMEM;

    for (i = 0; i < CARD_STEELHEAD_COUNT; ++i) {
        prop_enabled[i] = 1;
    }

    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->hw_device.common.module = (struct hw_module_t *) module;
    adev->hw_device.common.close = adev_close;

    ALOGV("%s %d: hw_device %p\n", __func__, __LINE__, &(adev->hw_device));
    adev->hw_device.get_supported_devices = adev_get_supported_devices;
    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_mute = adev_set_master_mute;
    adev->hw_device.get_master_mute = adev_get_master_mute;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    ALOGV("%s %d: open_output_stream %p\n", __func__, __LINE__, adev->hw_device.open_output_stream);
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.dump = adev_dump;

    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(&adev->lock, &mta);
    pthread_mutexattr_destroy(&mta);

    /* Set the default route before the PCM stream is opened */
    pthread_mutex_lock(&adev->lock);
    adev->mode = AUDIO_MODE_NORMAL;
    adev->devices = AUDIO_DEVICE_OUT_SPEAKER;

    adev->voice_volume = 1.0f;
    adev->master_volume = 1.0f;
    adev->tas5713_fd = -1;

    if(get_boardtype(adev)) {
        pthread_mutex_unlock(&adev->lock);
        free(adev);
        ALOGE("Unsupported boardtype, aborting.");
        return -EINVAL;
    }

    pthread_mutex_unlock(&adev->lock);
    *device = &adev->hw_device.common;

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "Tungsten Audio HW HAL",
        .author = "Howard M. Harte (hharte@magicandroidapps.com)",
        .methods = &hal_module_methods,
    },
};
