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

# These two variables are set first, so they can be overridden
# by BoardConfigVendor.mk
BOARD_USES_GENERIC_AUDIO := false
USE_CAMERA_STUB := true

#ENHANCED_DOMX := true
BLTSVILLE_ENHANCEMENT := false
USE_ITTIAM_AAC := true
#OMAP_ENHANCEMENT := true
#OMAP_ENHANCEMENT_MULTIGPU := true

#OMAP_ENHANCEMENT_CPCAM := true
#OMAP_ENHANCEMENT_VTC := true

TARGET_SPECIFIC_HEADER_PATH := device/google/steelhead/include

# Use the non-open-source parts, if they're present
-include vendor/google/steelhead/BoardConfigVendor.mk

TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_SMP := true
TARGET_CPU_VARIANT := cortex-a9
TARGET_ARCH:= arm
TARGET_ARCH_VARIANT := armv7-a-neon
ARCH_ARM_HAVE_TLS_REGISTER := true

BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_BCM := true
TARGET_NO_BOOTLOADER := true

BOARD_KERNEL_BASE := 0x80000000
#BOARD_KERNEL_CMDLINE :=
TARGET_KERNEL_CONFIG := cyanogenmod_steelhead_defconfig

TARGET_NO_RADIOIMAGE := true
TARGET_BOARD_PLATFORM := omap4
TARGET_BOOTLOADER_BOARD_NAME := steelhead 

# Recovery
TARGET_RECOVERY_PIXEL_FORMAT := "BGRA_8888"
BOARD_CUSTOM_RECOVERY_KEYMAPPING := ../../device/google/steelhead/recovery/recovery_keys.c
BOARD_HAS_NO_SELECT_BUTTON := true
# device-specific extensions to the updater binary
TARGET_RELEASETOOLS_EXTENSIONS := device/google/steelhead
TARGET_RECOVERY_FSTAB = device/google/steelhead/fstab.steelhead
RECOVERY_FSTAB_VERSION = 2

BOARD_EGL_CFG := device/google/steelhead/egl.cfg

#BOARD_USES_HGL := true
#BOARD_USES_OVERLAY := true
USE_OPENGL_RENDERER := true
BOARD_USES_PANDA_GRAPHICS := true

TARGET_USERIMAGES_USE_EXT4 := true
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1056857088
BOARD_USERDATAIMAGE_PARTITION_SIZE := 13897654272
BOARD_CACHEIMAGE_PARTITION_SIZE := 528424960
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_FLASH_BLOCK_SIZE := 4096

# Wifi related defines
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
WPA_SUPPLICANT_VERSION      := VER_0_8_X
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_bcmdhd
BOARD_HOSTAPD_DRIVER        := NL80211
BOARD_HOSTAPD_PRIVATE_LIB   := lib_driver_cmd_bcmdhd
BOARD_WLAN_DEVICE           := bcmdhd
WIFI_DRIVER_FW_PATH_PARAM   := "/sys/module/bcmdhd/parameters/firmware_path"
WIFI_DRIVER_FW_PATH_STA     := "/vendor/firmware/fw_bcmdhd.bin"
WIFI_DRIVER_FW_PATH_P2P     := "/vendor/firmware/fw_bcmdhd_p2p.bin"
WIFI_DRIVER_FW_PATH_AP      := "/vendor/firmware/fw_bcmdhd_apsta.bin"

#TARGET_PROVIDES_INIT_RC := true
#TARGET_USERIMAGES_SPARSE_EXT_DISABLED := true
BOARD_USES_SECURE_SERVICES := true

TARGET_HAS_WAITFORVSYNC := true
BOARD_USE_SYSFS_VSYNC_NOTIFICATION := true
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := device/google/steelhead/bluetooth
BOARD_BLUEDROID_VENDOR_CONF := device/google/steelhead/bluetooth/vnd.cfg

BOARD_SEPOLICY_DIRS := \
    device/google/steelhead/selinux

BOARD_SEPOLICY_UNION := \
    file_contexts \
    pvrsrvinit.te \
    device.te \
    domain.te

BOARD_HAL_STATIC_LIBRARIES := libhealthd.steelhead
