/*
**
** Copyright 2008, The Android Open Source Project
** Copyright 2010, Samsung Electronics Co. LTD
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef ANDROID_HARDWARE_CAMERA_HARDWARE_SEC_H
#define ANDROID_HARDWARE_CAMERA_HARDWARE_SEC_H

#include "UVCCamera.h"
#include <utils/threads.h>
#include <utils/RefBase.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <hardware/camera.h>
#include <hardware/gralloc.h>
#include <camera/CameraParameters.h>

namespace android {
    class CameraHardwareUVC : public virtual RefBase {
public:
    virtual void        setCallbacks(camera_notify_callback notify_cb,
                                     camera_data_callback data_cb,
                                     camera_data_timestamp_callback data_cb_timestamp,
                                     camera_request_memory get_memory,
                                     void *user);

    virtual void        enableMsgType(int32_t msgType);
    virtual void        disableMsgType(int32_t msgType);
    virtual bool        msgTypeEnabled(int32_t msgType);

    virtual status_t    startPreview();
    virtual void        stopPreview();
    virtual bool        previewEnabled();

    virtual status_t    startRecording();
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const void *opaque);

    virtual status_t    autoFocus();
    virtual status_t    cancelAutoFocus();
    virtual status_t    takePicture();
    virtual status_t    cancelPicture();
    virtual status_t    dump(int fd) const;
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual status_t    sendCommand(int32_t command, int32_t arg1, int32_t arg2);
    virtual status_t    setPreviewWindow(preview_stream_ops *w);
    virtual status_t    storeMetaDataInBuffers(bool enable);
    virtual void        release();

    inline  int         getCameraId() const;

    CameraHardwareUVC(int cameraId, camera_device_t *dev);
    virtual             ~CameraHardwareUVC();
private:
    status_t    previewInit();
    void stopPreviewLocked();
    void freePreviewHeap();

    static  const int   kBufferCount = MAX_BUFFERS;
    static  const int   kBufferCountForRecord = MAX_BUFFERS;

    class PreviewThread : public Thread {
        CameraHardwareUVC *mHardware;
    public:
        PreviewThread(CameraHardwareUVC *hw):
        Thread(false),
        mHardware(hw) { }
        virtual void startPreview() {
            run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewThreadWrapper();
            return false;
        }
    };

    class PictureThread : public Thread {
        CameraHardwareUVC *mHardware;
    public:
        PictureThread(CameraHardwareUVC *hw):
        Thread(false),
        mHardware(hw) { }
        virtual bool threadLoop() {
            mHardware->pictureThread();
            return false;
        }
    };

            void        initDefaultParameters(int cameraId);
            void        initHeapLocked();

    sp<PreviewThread>   mPreviewThread;
            int         previewThread();
            int         previewThreadWrapper();

    sp<PictureThread>   mPictureThread;
            int         pictureThread();
            bool        mCaptureInProgress;

            int         save_jpeg(unsigned char *real_jpeg, int jpeg_size);
            void        save_postview(const char *fname, uint8_t *buf,
                                        uint32_t size);
            int         decodeInterleaveData(unsigned char *pInterleaveData,
                                                int interleaveDataSize,
                                                int yuvWidth,
                                                int yuvHeight,
                                                int *pJpegSize,
                                                void *pJpegData,
                                                void *pYuvData);
            bool        YUY2toNV21(void *srcBuf, void *dstBuf, uint32_t srcWidth, uint32_t srcHeight);
            bool        scaleDownYuv422(char *srcBuf, uint32_t srcWidth,
                                        uint32_t srcHight, char *dstBuf,
                                        uint32_t dstWidth, uint32_t dstHight);

            bool        CheckVideoStartMarker(unsigned char *pBuf);
            bool        CheckEOIMarker(unsigned char *pBuf);
            bool        FindEOIMarkerInJPEG(unsigned char *pBuf,
                                            int dwBufSize, int *pnJPEGsize);
            bool        SplitFrame(unsigned char *pFrame, int dwSize,
                                   int dwJPEGLineLength, int dwVideoLineLength,
                                   int dwVideoHeight, void *pJPEG,
                                   int *pdwJPEGSize, void *pVideo,
                                   int *pdwVideoSize);
            bool        isSupportedPreviewSize(const int width,
                                               const int height) const;
            bool        isSupportedParameter(const char * const parm,
                            const char * const supported_parm) const;
            status_t    waitCaptureCompletion();
    /* used by auto focus thread to block until it's told to run */
    mutable Mutex       mFocusLock;
    mutable Condition   mFocusCondition;

    /* used by preview thread to block until it's told to run */
    mutable Mutex       mPreviewLock;
    /* Protects preview start/stop operations so a new preview thread
       can't start while we wait for an existing one to exit. */
    mutable Mutex       mPreviewStartStopLock;
    mutable Condition   mPreviewCondition;
    mutable Condition   mPreviewStoppedCondition;
    /* mPreviewRunning and mPreviewWindow are used as conditions
       in the preview thread. */
    bool                mPreviewRunning;
    preview_stream_ops  *mPreviewWindow;

    /* used to guard mCaptureInProgress */
    mutable Mutex       mCaptureLock;
    mutable Condition   mCaptureCondition;

    CameraParameters    mParameters;
    CameraParameters    mInternalParameters;

    MemoryHeapBase      *mPreviewHeap[MAX_BUFFERS];
    camera_memory_t     *mRawHeap;
    camera_memory_t     *mRecordHeap;

    UVCCamera           *mUVCCamera;
            const __u8  *mCameraSensorName;

    camera_notify_callback     mNotifyCb;
    camera_data_callback       mDataCb;
    camera_data_timestamp_callback mDataCbTimestamp;
    camera_request_memory      mGetMemoryCb;
            void        *mCallbackCookie;

            int32_t     mMsgEnabled;

            bool        mRecordRunning;
    mutable Mutex       mRecordLock;
            int         mPostViewWidth;
            int         mPostViewHeight;
            int         mPostViewSize;

            Vector<Size> mSupportedPreviewSizes;

    camera_device_t *mHalDevice;
    static gralloc_module_t const* mGrallocHal;
};

}; // namespace android

#endif
