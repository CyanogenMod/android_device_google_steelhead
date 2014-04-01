# Copyright 2013 The Android Open Source Project

ifneq ($(BUILD_TINY_ANDROID),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := healthd_board_steelhead.cpp
LOCAL_MODULE := libhealthd.steelhead
LOCAL_C_INCLUDES := += system/core/healthd
include $(BUILD_STATIC_LIBRARY)

endif
