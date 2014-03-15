# Inherit some common CM stuff.
$(call inherit-product, vendor/cm/config/common_full_tablet_wifionly.mk)

# Enhanced NFC
$(call inherit-product, vendor/cm/config/nfc_enhanced.mk)

# Inherit device configuration
$(call inherit-product, device/google/steelhead/full_steelhead.mk)

## Device identifier. This must come after all inclusions
PRODUCT_DEVICE := steelhead
PRODUCT_NAME := cm_steelhead
PRODUCT_BRAND := Google
PRODUCT_MODEL := Nexus Q
PRODUCT_MANUFACTURER := Google

PRODUCT_BUILD_PROP_OVERRIDES += \
	PRODUCT_NAME="tungsten" \
	BUILD_FINGERPRINT="google/tungsten/phantasm:$(PLATFORM_VERSION)/$(BUILD_ID)/$(shell date +%Y%m%d%H%M%S):user/release-keys" \
	PRIVATE_BUILD_DESC="tungsten-user $(PLATFORM_VERSION) $(BUILD_ID) $(shell date +%Y%m%d%H%M%S) release-keys"
