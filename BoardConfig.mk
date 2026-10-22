# Copyright (C) 2025 The OrangeFox for pd2415 contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Modeled on device_xiaomi_dali_twrp/BoardConfig.mk. TW_/OF_/FOX_ build flags
# live HERE; twrp_pd2415.mk stays a minimal product makefile.

DEVICE_PATH := device/vivo/pd2415
TARGET_COPY_OUT_VENDOR := vendor
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/recovery.fstab
BOARD_VENDOR_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy
TW_USES_VENDOR_LIBS := true

# --- arch --------------------------------------------------------------------
TARGET_ARCH := arm64
# MediaTek MT6991 (Dimensity 9400) kernel is armv9-a (same SoC as dali).
TARGET_ARCH_VARIANT := armv9-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_ABI2 :=
TARGET_CPU_VARIANT := generic
TARGET_SUPPORTS_64_BIT_APPS := true

TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true
TARGET_NO_RECOVERY := true

# --- A/B ---------------------------------------------------------------------
AB_OTA_UPDATER := true
OF_USE_AIDL_BOOT_CONTROL := 1
# Match the device's dynamic partition set (from stock fstab.mt6991).
AB_OTA_PARTITIONS := boot dtbo init_boot odm odm_dlkm product system system_dlkm system_ext vbmeta vbmeta_system vbmeta_vendor vendor vendor_boot vendor_dlkm

# --- boot image (vendor_boot v4) ---------------------------------------------
# Sizes/offsets/cmdline copied verbatim from the stock vendor_boot.img
# (unpack_bootimg --boot_img stock_vendor_boot.img).
# Source: F:\learn-front\learn-hook\vivo-x200-pm-vendor-boot\mod\vendor_boot.json
BOARD_BOOT_HEADER_VERSION := 4
# From /proc/partitions on device: boot_b=98304 blocks, init_boot_b=8192 blocks
BOARD_BOOTIMAGE_PARTITION_SIZE := 100663296
BOARD_INIT_BOOT_IMAGE_PARTITION_SIZE := 8388608
BOARD_VENDOR_BOOTIMAGE_PARTITION_SIZE := 134217728

BOARD_KERNEL_BASE := 0x80000000
BOARD_KERNEL_PAGESIZE := 4096
BOARD_KERNEL_CMDLINE := bootopt=64S3,32N2,64N2 product.version=PD2415_A_15.0.33.7.W10 fingerprint.abbr=15/AP3A.240905.015.A1 region_ver=W10 product.solution=MTK
# IMPORTANT: --*_offset values are RELATIVE to BOARD_KERNEL_BASE, but the
# numbers in vendor_boot.json (ramdisk=0xa4b00000, tags=0x87c80000,
# dtb=0x87c80000) are ABSOLUTE load addresses. mkbootimg writes
# kernel_addr/ramdisk_addr/tags_addr as 32-bit 'I' fields computed as
# base + offset, so feeding the absolute values back as offsets overflows
# 2^32 (0x80000000+0xa4b00000 = 0x124B00000) and aborts with
# "struct.error: 'I' format requires 0 <= number <= 4294967295".
# Therefore subtract the base here so the final absolute addresses stay
# identical to stock:
#   kernel  : 0x80000000 - 0x80000000 = 0x00000000
#   ramdisk : 0xa4b00000 - 0x80000000 = 0x24b00000
#   tags    : 0x87c80000 - 0x80000000 = 0x07c80000
#   dtb     : 0x87c80000 - 0x80000000 = 0x07c80000
#
# Do NOT read the ramdisk address off vendor_boot.json's "loadAddr" by eye:
# it is DECIMAL (2762997760). 2762997760 - 0x80000000 = 615514112 = 0x24B00000,
# i.e. 0xA4B00000 — NOT 0xA4D00000. Writing 0x24d00000 shifts the vendor
# ramdisk 2 MiB above the stock load address and the LK then hands the kernel
# a bogus initrd address (no boot, no recovery).
BOARD_MKBOOTIMG_ARGS := --header_version 4 --kernel_offset 0x00000000 --ramdisk_offset 0x24b00000 --tags_offset 0x07c80000 --dtb_offset 0x07c80000

TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/kernel
BOARD_INCLUDE_DTB_IN_BOOTIMG := true
BOARD_PREBUILT_DTBIMAGE_DIR := $(DEVICE_PATH)/prebuilt/dtb

# Recovery resources + ramdisk are packed into vendor_boot, not boot.
BOARD_MOVE_RECOVERY_RESOURCES_TO_VENDOR_BOOT := true
BOARD_INCLUDE_RECOVERY_RAMDISK_IN_VENDOR_BOOT := true

# --- display -------------------------------------------------------------------
TW_THEME := portrait_hdpi
TARGET_RECOVERY_PIXEL_FORMAT := RGBX_8888
TW_BRIGHTNESS_PATH := "/sys/class/leds/lcd-backlight/brightness"
# TODO: read from the stock panel — Dimensity 9400 panels typically use 12-bit
# (4095 or 2047). dali uses 2047; confirm on PD2415.
TW_MAX_BRIGHTNESS := 2047
TW_DEFAULT_BRIGHTNESS := 512

# --- size budget (the platform fragment shares vendor_boot) --------------------
TW_EXCLUDE_DEFAULT_USB_INIT := true
TW_EXCLUDE_BASH := true
TW_EXCLUDE_NANO := true
TW_EXCLUDE_TZDATA := true
TW_EXCLUDE_LPDUMP := true
TW_EXCLUDE_LPTOOLS := true
TW_EXCLUDE_APEX := true
TW_USE_LEGACY_BATTERY_SERVICES := true

# --- ramdisk preparation hook --------------------------------------------------
# Runs after ordinary Recovery relinking and the initial manifests, before
# mkbootfs packages the final root; stages modules / props / HAL / firmware.
BOARD_RECOVERY_IMAGE_PREPARE = $(DEVICE_PATH)/recovery/prepare-ramdisk.sh $(TARGET_RECOVERY_ROOT_OUT) $(SOONG_OUT_DIR)/Android-$(TARGET_PRODUCT).mk $(abspath $(LLVM_READOBJ)) $(abspath $(DEVICE_PATH)/prebuilt/recovery_modules) $(abspath $(DEVICE_PATH)/prebuilt/recovery_properties) $(abspath $(DEVICE_PATH)/prebuilt/recovery_vendor_hal) $(abspath $(DEVICE_PATH)/prebuilt/recovery_firmware)

# pd2415_recovery_prepare_marker is the phony guard package in Android.mk.
# NOTE: use the PLATFORM `servicemanager` binary, NOT `servicemanager.recovery`.
# The recovery-variant binary links against the recovery variant of libvintf
# and requires VintfObjectRecovery::GetInstance, but the libvintf.so that
# lands in the recovery ramdisk is the PLATFORM variant (no such symbol), so
# the recovery-variant binary dies with "CANNOT LINK EXECUTABLE" -> exit(1)
# before main() runs -> servicemanager crash loop -> recovery never boots.
# The platform binary needs only VintfObject (present in the ramdisk's
# platform-variant libvintf.so) and runs fine (verified on device).
TARGET_RECOVERY_DEVICE_MODULES += servicemanager pd2415_recovery_prepare_marker dmctl

TW_RECOVERY_ADDITIONAL_RELINK_LIBRARY_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libsysutils.so

# --- flashlight -------------------------------------------------------------------
# TODO: find the correct sysfs paths on PD2415 before enabling.
#   OF_FLASHLIGHT_ENABLE := 1
#   OF_FL_PATH1 := /sys/class/leds/white:flash-1
#   OF_FL_PATH2 := /sys/class/leds/white:flash-2

# --- status bar ---------------------------------------------------------------------
# TODO: confirm indent values on PD2415 panel.
#   OF_STATUS_INDENT_LEFT := 130
#   OF_STATUS_INDENT_RIGHT := 130
# Recovery ships without tzdata (TW_EXCLUDE_TZDATA); use POSIX TZ string.
OF_DEFAULT_TIMEZONE := TAIST-8

# --- crypto ---------------------------------------------------------------------
TW_INCLUDE_CRYPTO := true
# TODO: enable if PD2415 has an eSE/OMAPI service.
# TW_INCLUDE_OMAPI := true
# $(call soong_config_set,omapi,uuid,<OMAPI_UUID>)

# --- modules ----------------------------------------------------------------------
# Boolean switch + quoted module name list (never PRODUCT_COPY_FILES).
# The .ko files behind this list live in the PLATFORM fragment: recovery mode
# loads vendor_ramdisk00 (platform) AND vendor_ramdisk01 (recovery), and the
# stock platform ramdisk already ships the whole /lib/modules closure — the
# official recovery ramdisk contains no .ko at all. So
# recovery/prepare-ramdisk.sh stages ONLY the modules the platform fragment is
# missing (normally none); copying all 337 again added ~70 MB to the recovery
# fragment and pushed vendor_boot past the LK's load window.
# Module list copied verbatim from stock platform ramdisk modules.load
# (F:\learn-front\learn-hook\vivo-x200-pm-vendor-boot\mod\root.1\lib\modules\modules.load).
TW_LOAD_VENDOR_BOOT_MODULES := true
TW_LOAD_VENDOR_MODULES := "8250_mtk.ko adapter_class.ko aee_aed.ko aee_hangdet.ko aee_rs.ko arm_dsu_pmu.ko arm_smmu_v3.ko blk-enhance.ko blocktag.ko bootprof.ko bus-parity.ko cache-parity.ko clk-chk-mt6991.ko clk-common.ko clk-dbg-mt6991.ko clk-fmeter-mt6991.ko clk-mt6991-adsp.ko clk-mt6991-bus.ko clk-mt6991-cam.ko clk-mt6991-img.ko clk-mt6991-mdpsys.ko clk-mt6991-mmsys.ko clk-mt6991-peri.ko clk-mt6991-vcodec.ko clk-mt6991.ko clkbuf.ko cmdq-platform-mt6991.ko cmdq_helper_inf.ko cqhci.ko dbg_error_flag.ko dbgtop-drm.ko device-apc-common.ko device-apc-mt6991.ko drm_display_helper.ko drm_dma_helper.ko emi-mpu.ko emi-slb.ko emi.ko event_track.ko extcon-mtk-usb.ko extdev_io_class.ko ffa_v11.ko fp_dispatch_event.ko gic-ram-parity.ko handshake_counter.ko i2c-mt65xx.ko industrialio-triggered-buffer.ko iommu_debug.ko irq-dbg.ko ise-trusty-ipc.ko ise-trusty-log.ko ise-trusty-virtio.ko ise-trusty.ko ise_lpm.ko isee-ffa.ko kfifo_buf.ko last_bus.ko leds-mtk-disp.ko leds-mtk-pwm.ko leds-mtk.ko libarc4.ko load_track.ko log_store.ko mcDrvModule-ffa.ko mdp_drv_mt6991.ko mediatek-cpufreq-hw.ko mediatek-drm.ko mkp.ko mm-fake-engine.ko mmprofile.ko mmqos-common.ko mmqos-mt6991.ko monitor_hang.ko mrdump.ko mt6316-regulator.ko mt6363-regulator.ko mt6373-regulator.ko mt6375-auxadc.ko mt6379-adc.ko mt6379-regulator.ko mt6379s.ko mt6681-auxadc.ko mt6681-core.ko mt6681-regulator.ko mt6991_dcm.ko mtk-afe-external.ko mtk-cmdq-drv-ext.ko mtk-dvfsrc-devfreq.ko mtk-dvfsrc-helper.ko mtk-dvfsrc-regulator.ko mtk-dvfsrc.ko mtk-emi.ko mtk-hw-semaphore.ko mtk-i3c-i2c-wrap.ko mtk-i3c-master-mt69xx.ko mtk-icc-core.ko mtk-ise-mailbox.ko mtk-mbox-mailbox.ko mtk-mbox.ko mtk-mmc-dbg.ko mtk-mmc.ko mtk-mmdebug-vcp.ko mtk-mmdvfs-debug.ko mtk-mmdvfs-ftrace.ko mtk-mmdvfs-v3-start.ko mtk-mmdvfs-v3.ko mtk-mmdvfs.ko mtk-mminfra-debug.ko mtk-mminfra-imax.ko mtk-mml-mt6991.ko mtk-mml.ko mtk-pmic-keys.ko mtk-pmic-wrap.ko mtk-scpsys-mt6991-mmpc.ko mtk-scpsys-mt6991-spm.ko mtk-scpsys.ko mtk-smi-dbg.ko mtk-smi.ko mtk-socinfo.ko mtk-spmi-pmic-adc.ko mtk-spmi-pmic.ko mtk-swpm-perf-arm-pmu.ko mtk-uart-apdma.ko mtk-vmm-notifier-mt6991.ko mtk_battery_oc_throttling.ko mtk_bp_thl.ko mtk_cpu_power_throttling.ko mtk_dcm.ko mtk_disp_notify.ko mtk_dpc_v2.ko mtk_dramc.ko mtk_iommu_util.ko mtk_low_battery_throttling.ko mtk_panel_ext.ko mtk_printk_ctrl.ko mtk_rpmsg_mbox.ko mtk_slbc.ko mtk_sync.ko mtk_tinysys_ipi.ko mtk_vdisp_v2.ko mtk_wdt.ko mtu3.ko mux_switch.ko nvmem-mt6681-efuse.ko nvmem_mtk-devinfo.ko panel-alpha-jdi-nt36672e-cphy-vdo.ko panel-alpha-jdi-nt36672e-vdo-120hz.ko panel-alpha-jdi-nt36672e-vdo-60hz.ko panel-nt37801-cmd-fhd-plus.ko panel-nt37801-cmd-fhd.ko panel-truly-nt35595-cmd.ko panel-truly-td4330-cmd.ko panel_common.ko panel_virtual.ko pcie-mediatek-gen3.ko pd-chk-mt6991.ko pd_dbg_info.ko phy-mtk-mt6379-eusb2-repeater.ko phy-mtk-nxp-eusb2-repeater.ko phy-mtk-pcie.ko phy-mtk-ufs.ko phy-mtk-xsphy.ko pidmap.ko pie_driver.ko pinctrl-mt6363.ko pinctrl-mt6373.ko pinctrl-mt6991.ko pinctrl-mtk-common-v2_debug.ko pinctrl-mtk-v2.ko pkvm_mgmt.ko pkvm_mkp.ko pkvm_smmu.ko pkvm_tmem.ko pmic_lbat_service.ko pmic_lvsys_notify.ko ps5170.ko pwm-mtk-disp.ko reboot-mode.ko regulator-vibrator.ko reset-ti-syscon.ko rpmb-mtk.ko rpmb.ko rt4803.ko rt4831a_drv.ko rt6160-regulator.ko rtc-mt6685.ko sec-rng.ko sec.ko sensors_class.ko shrinker_proxy.ko slbc_ipi.ko slbc_trace.ko slc-parity.ko smap-mt6991.ko smmu_secure.ko smpu-hook-v1.ko smpu.ko sn100.ko sn100_spi.ko spi-mt65xx.ko spmi-mtk-pmif.ko symphony.ko syscon-reboot-mode.ko system_heap.ko systracker.ko tcpc_class.ko tcpci_late_sync.ko teeperf.ko thp_pool.ko timer-mediatek.ko tinysys-scmi.ko ufs-mediatek-dbg.ko ufs-mediatek-mod-ise.ko usb_boost.ko usb_dp_selector.ko usb_meta.ko v4l2-flash-led-class.ko v_zsmalloc.ko vcp_status_v2.ko vcp_v2.ko vfcs-misc-core.ko vfcs-vpsy.ko vivo_board_info.ko vivo_board_info_detect.ko vivo_bsp_engine.ko vivo_display.ko vivo_rpk_ctrl.ko vivo_rsc.ko vivo_rsmc_driver.ko vivo_tcp.ko vivo_ts.ko vivo_wifi_driver.ko vkyber-iosched.ko vsed.ko xhci-mtk-hcd-v2.ko zcache.ko"

# --- identity / localization ------------------------------------------------------
OF_MAINTAINER := LongMo
TW_DEFAULT_LANGUAGE := zh_CN
FOX_USE_DATA_RECOVERY_FOR_SETTINGS := 1

# --- vendor ramdisk platform fragment -------------------------------------------
# package-vendor-boot.sh rebuilds vendor_ramdisk00 from the prebuilt stock platform
# ramdisk (prebuilt/vendor_ramdisk/platform.cpio.gz) and only reads the built
# vendor_boot.img to extract vendor_ramdisk01 (the recovery fragment). Soong's
# vendorbootimage rule still mkbootfs TARGET_VENDOR_RAMDISK_OUT into vendor_ramdisk00;
# without any fragment that directory is empty and mkbootfs fails ("cannot open
# directory"), yielding an empty vendor_ramdisk00 warning. This minimal first-stage
# fstab fragment gives mkbootfs real content so the build is clean. It is overwritten
# by the prebuilt platform ramdisk at packaging time and has no functional effect.
BOARD_VENDOR_RAMDISK_FRAGMENTS += pd2415