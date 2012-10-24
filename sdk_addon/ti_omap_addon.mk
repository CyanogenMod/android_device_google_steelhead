#
# Copyright (C) 2012 Texas Instruments
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

# List of apps and optional libraries (Java and native) to put in the add-on system image.
PRODUCT_PACKAGES := \
	com.ti.omap.android.cpcam \
	libcpcam_jni

# Manually copy the optional library XML files in the system image.
PRODUCT_COPY_FILES := \
    hardware/ti/omap4xxx/cpcam/com.ti.omap.android.cpcam.xml:system/etc/permissions/com.ti.omap.android.cpcam.xml

# name of the add-on
PRODUCT_SDK_ADDON_NAME := cpcam

# Copy the manifest and hardware files for the SDK add-on.
# The content of those files is manually created for now.
PRODUCT_SDK_ADDON_COPY_FILES := \
    device/google/steelhead/sdk_addon/manifest.ini:manifest.ini \
    device/google/steelhead/sdk_addon/hardware.ini:hardware.ini \
	$(call find-copy-subdir-files,*,device/sample/skins/WVGAMedDpi,skins/WVGAMedDpi)


# Add this to PRODUCT_SDK_ADDON_COPY_FILES to copy the files for an
# emulator skin (or for samples)
#$(call find-copy-subdir-files,*,device/sample/skins/WVGAMedDpi,skins/WVGAMedDpi)

# Copy the jar files for the optional libraries that are exposed as APIs.
PRODUCT_SDK_ADDON_COPY_MODULES := \
    com.ti.omap.android.cpcam:libs/cpcam.jar

# FIXME The build system doesnt build the stub defs below but the s3d stubs defined in device/ti/common/s3d
# As a workaround, cpcam stub defs are added into s3d stub defs
PRODUCT_SDK_ADDON_STUB_DEFS += \
    device/google/steelhead/sdk_addon/cpcam_stub_defs.txt

# Name of the doc to generate and put in the add-on. This must match the name defined
# in the optional library with the tag
#    LOCAL_MODULE:= platform_library
# in the documentation section.
PRODUCT_SDK_ADDON_DOC_MODULES := cpcam

# This add-on extends the default sdk product.
$(call inherit-product, $(SRC_TARGET_DIR)/product/sdk.mk)

# Real name of the add-on. This is the name used to build the add-on.
# Use 'make PRODUCT-<PRODUCT_NAME>-sdk_addon' to build the add-on.
PRODUCT_NAME := ti_omap_addon
