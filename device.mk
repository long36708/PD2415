

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
# Recovery 属性环境对齐 OS6 固件 (2026-06-01): keymint 上报的补丁级别必须与
# 设备 keyblob 一致, 否则触发 REQUIRES_UPGRADE → 升级半完成写毒药 upgraded_file
# (设备实测, 三处位点). v1 基线没有此项, 每次启动解密尝试都会重新写毒 ——
# 本分支补上, 保护用户密钥.
PLATFORM_SECURITY_PATCH := 2026-06-01
PLATFORM_VENDOR_SECURITY_PATCH := 2026-06-01

# fastbootd (userspace fastboot, 动态分区刷写): 二进制与接口库由 TW_INCLUDE_FASTBOOTD
# 带入, 这里按 emira/rodin 参考树显式声明包; ro.fastbootd.available 是 recovery
# 二进制运行时读取的可用性门控 (参考树 samsung a50/tama 配方), 缺它 fastbootd
# 页面会拒绝启动 (E:Failed to start fastbootd page.)。属性经 prop.default 生效,
# 与 ro.twrp.vendor_boot 同机制。
PRODUCT_PACKAGES += \
    fastbootd \
    android.hardware.fastboot@1.0 \
    android.hardware.fastboot@1.1

PRODUCT_PROPERTY_OVERRIDES += \
    ro.fastbootd.available=true
