#
# Copyright 2012 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := lightsctl.c
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)
LOCAL_SHARED_LIBRARIES := libcutils
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := avrlights
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := hsv2rgb.c
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)
LOCAL_SHARED_LIBRARIES := libcutils
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := hsv2rgb
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := lights.c
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SHARED_LIBRARIES := liblog \
	libcutils

LOCAL_MODULE := lights.steelhead
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

