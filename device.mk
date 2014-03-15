#
# Copyright (C) 2012 The Android Open-Source Project
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

PRODUCT_COPY_FILES := \
	device/google/steelhead/init.steelhead.rc:root/init.steelhead.rc \
	device/google/steelhead/init.steelhead.usb.rc:root/init.steelhead.usb.rc \
        device/google/steelhead/fstab.steelhead:root/fstab.steelhead \
	device/google/steelhead/ueventd.steelhead.rc:root/ueventd.steelhead.rc \
	device/google/steelhead/media_profiles.xml:system/etc/media_profiles.xml \
	device/google/steelhead/media_codecs.xml:system/etc/media_codecs.xml \
	device/google/steelhead/android.hardware.bluetooth.xml:system/etc/permissions/android.hardware.bluetooth.xml \
	frameworks/native/data/etc/android.hardware.usb.host.xml:system/etc/permissions/android.hardware.usb.host.xml \
	frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
	frameworks/native/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
	frameworks/native/data/etc/com.android.nfc_extras.xml:system/etc/permissions/com.android.nfc_extras.xml \
	frameworks/native/data/etc/android.hardware.nfc.xml:system/etc/permissions/android.hardware.nfc.xml \
	frameworks/native/data/etc/tablet_core_hardware.xml:system/etc/permissions/tablet_core_hardware.xml

## Adjust recovery resolution

PRODUCT_COPY_FILES += \
	device/google/steelhead/recovery/postrecoveryboot.sh:recovery/root/sbin/postrecoveryboot.sh

PRODUCT_PACKAGES := \
        make_ext4fs \
	com.android.future.usb.accessory

PRODUCT_PROPERTY_OVERRIDES := \
	ro.sf.lcd_density=213 \
	ro.zygote.disable_gl_preload=true \
	ro.opengles.version=131072 \
	hwui.render_dirty_regions=false \
	drm.service.enabled=true \
	media.stagefright.cache-params=10240/20480/15 \
	wifi.interface=wlan0 \
	wifi.supplicant_scan_interval=15

PRODUCT_CHARACTERISTICS := tablet,nosdcard

DEVICE_PACKAGE_OVERLAYS := \
    device/google/steelhead/overlay

#HWC Hal
PRODUCT_PACKAGES += \
    hwcomposer.omap4

PRODUCT_TAGS += dalvik.gc.type-precise

PRODUCT_PACKAGES += \
	librs_jni \
	com.android.future.usb.accessory

# Audio
PRODUCT_PACKAGES += \
	audio.primary.steelhead \
	audio.a2dp.default

PRODUCT_COPY_FILES += \
	device/google/steelhead/audio/audio_policy.conf:system/etc/audio_policy.conf

PRODUCT_PACKAGES += \
	dhcpcd.conf \
	TQS_D_1.7.ini \
	calibrator

# Filesystem management tools
PRODUCT_PACKAGES += \
	make_ext4fs \
	setup_fs

PRODUCT_PROPERTY_OVERRIDES += \

$(call inherit-product-if-exists, vendor/google/steelhead/device-vendor.mk)
$(call inherit-product, frameworks/native/build/tablet-dalvik-heap.mk)
$(call inherit-product, hardware/ti/omap4xxx/omap4.mk)

PRODUCT_PACKAGES += \
	steelhead_hdcp_keys

PRODUCT_COPY_FILES += \
        device/google/steelhead/bcmdhd.cal:system/etc/wifi/bcmdhd.cal

# NFC
PRODUCT_PACKAGES += \
        libnfc \
        libnfc_jni \
        Nfc \
        nfc.steelhead

PRODUCT_COPY_FILES += \
        device/google/steelhead/smc_normal_world_android_cfg.ini:system/vendor/etc/smc_normal_world_android_cfg.ini

PRODUCT_PACKAGES += \
        common_time \
        lights.steelhead \
        avrlights \
        hsv2rgb \
        camera.steelhead


PRODUCT_AAPT_CONFIG := normal large tvdpi hdpi
PRODUCT_AAPT_PREF_CONFIG := tvdpi
