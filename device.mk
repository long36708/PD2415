

# API
PRODUCT_SHIPPING_API_LEVEL := 31
PRODUCT_TARGET_VNDK_VERSION := 31

# Dynamic
PRODUCT_USE_DYNAMIC_PARTITIONS := true

# A/B
AB_OTA_UPDATER := true

PRODUCT_PACKAGES += \
    update_engine \
    update_engine_sideload \
    update_verifier \
    checkpoint_gc

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_system=true \
    POSTINSTALL_PATH_system=system/bin/mtk_plpath_utils \
    FILESYSTEM_TYPE_system=erofs \
    POSTINSTALL_OPTIONAL_system=true

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_vendor=true \
    POSTINSTALL_PATH_vendor=bin/checkpoint_gc \
    FILESYSTEM_TYPE_vendor=erofs \
    POSTINSTALL_OPTIONAL_vendor=true

# bootctrl
PRODUCT_PACKAGES += \
    android.hardware.boot@1.2-mtkimpl \
    android.hardware.boot@1.2-mtkimpl.recovery

PRODUCT_PACKAGES_DEBUG += \
    bootctrl

# health
PRODUCT_PACKAGES += \
    android.hardware.health@2.1-impl \
    android.hardware.health@2.1-service

# mtk plpath utils
PRODUCT_PACKAGES += \
    mtk_plpath_utils \
    mtk_plpath_utils.recovery

# keystore2
PRODUCT_PACKAGES += \
    android.system.keystore2

# keymint
PRODUCT_PACKAGES += \
    android.hardware.security.keymint \
    android.hardware.security.secureclock \
    android.hardware.security.sharedsecret

# drm
PRODUCT_PACKAGES += \
    android.hardware.drm@1.4

# keymaster
PRODUCT_PACKAGES += \
    android.hardware.keymaster@4.1

# otacert
PRODUCT_EXTRA_RECOVERY_KEYS += \
    $(DEVICE_PATH)/security/vivo_ota

# Additional Target Libraries
TARGET_RECOVERY_DEVICE_MODULES += \
    android.hardware.keymaster@4.1

TW_RECOVERY_ADDITIONAL_RELINK_LIBRARY_FILES += \
    $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.keymaster@4.1.so
# Recovery 属性环境对齐 OS6 固件 (2026-06-01):
# keymint HAL 在启动时把 OS/Vendor/Boot 补丁级别上报给 TEE; recovery 里的
# 属性还是 fox_14.1 树的旧值 (Android 14 时代), 与正常系统 (OS6, 2026-06-01)
# 创建的 keyblob 补丁级别不一致 → 触发 REQUIRES_UPGRADE → 升级又因环境不匹配
# 失败 (设备实测: metadata key 的 upgraded_file 半完成, TWRP createOperation
# -38, 正常系统锁屏凭据解包失败). recovery 属性必须与设备固件一致.
# 直接改 build 变量 (prop.default 由 sysprop.mk 从这两个变量生成),
# 而不是 PROPERTY_OVERRIDES (会与 buildinfo 生成重复定义).
# 注意: 不要动 PLATFORM_SECURITY_PATCH_TIMESTAMP —— BoardConfig 的
# BOARD_AVB_ROLLBACK_INDEX 引用它, 必须保持原值.
PLATFORM_SECURITY_PATCH := 2026-06-01
PLATFORM_VENDOR_SECURITY_PATCH := 2026-06-01
