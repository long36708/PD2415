# vivo X200 系列（X200 / X200 Pro mini / X200s）TWRP / OrangeFox 设备树

本仓库是 **vivo X200 系列**（vivo X200 / vivo X200 Pro mini / vivo X200s）的 TWRP 与
OrangeFox Recovery 设备树，覆盖设备代号 **PD2415 / PD2419 / PD2458**。
设备基于联发科 **MT6991** 平台，采用 arm64 架构、虚拟 A/B（Virtual A/B）动态分区方案。

> 注：PD2415 与 PD2419 共用同一套固件（vivo 共固件策略），运行态的
> `ro.product.device` / `model` / `name` 会随具体机型变化，但固件层属性（如
> `ro.product.bootimage.*`、`ro.product.system.*` 等）全部为 PD2415；本设备树按
> 固件层命名，主设备代号使用 `PD2415`。X200s（PD2458）为同系列设备，同样适用本树。

## 支持的设备

| 机型 | 型号 | 设备代号 |
| --- | --- | --- |
| vivo X200 | V2415A | PD2415 |
| vivo X200 Pro mini | V2419A | PD2419 |
| vivo X200s | V2458A | PD2458 |

## 设备规格

| 项目 | 规格 |
| --- | --- |
| 平台 | MediaTek MT6991（arm64 / armv8-a） |
| 架构 | arm64（TARGET_ARCH := arm64） |
| Recovery 载体 | `vendor_boot`（header v4，recovery 以 type-2 片段打包） |
| 分区方案 | 虚拟 A/B + 动态分区（Super，erofs） |
| 屏幕 | 1440 × 3200（密度 560，纵向） |
| 设备出厂 Android 版本 | 16 |
| 构建源码树（PLATFORM_VERSION） | 14（OrangeFox 14.1 分支，与设备 OS 版本相互独立） |
| 已验证固件基线 | `product.version=PD2415_A_16.1.12.28.W10` |
| 安全补丁级别 | 2026-06-01（与设备 keyblob 对齐，避免解密后写毒） |

> 说明：设备出厂 Android 16 指手机 OS 版本；Recovery 由 Android 14 源码树
> （OrangeFox 14.1）构建，二者互不影响，这是 TWRP 移植的常态。

## Boot 关键参数

下列值与官方 `vendor_boot` 一致，取自 `BoardConfig.mk`：

| 项目 | 值 |
| --- | --- |
| 页面大小（Page size） | 4096 |
| 内核基地址（Kernel base） | `0x80000000` |
| Ramdisk 偏移 | `0x24b00000` |
| Tags / DTB 偏移 | `0x07c80000` |
| Header 版本 | 4 |
| 内核命令行（cmdline） | `bootopt=64S3,32N2,64N2 erofs.reserved_pages=256 product.version=PD2415_A_16.1.12.28.W10 fingerprint.abbr=15/AP3A.240905.015.A2 region_ver=W10 product.solution=MTK` |
| DTB 来源 | `prebuilt/dtb/`（官方 vendor_boot 解包的 DTB 容器，**不能**由 `dtbo.img` 替代） |

## 功能状态

### 已配置 / 预期可用

- Recovery 启动（recovery 打包进 `vendor_boot`）
- 显示（1440×3200，TW_THEME=portrait_hdpi）
- 触控（TW_INPUT_BLACKLIST 屏蔽 `hbtp_vm`）
- 亮度调节（TW_DEFAULT_BRIGHTNESS=1000 / TW_MAX_BRIGHTNESS=1200）
- ADB / Fastbootd（TW_INCLUDE_FASTBOOTD）
- MTP（ro.sys.usb.storage.type=mtp）
- 动态分区可见（Super + 动态分区）
- 备份 / 还原（TWRP）
- 刷机（ZIP / Image）
- 恢复出厂（Factory Reset）
- 解密 FBE（已做 keyblob 对齐与特殊处理，详见下）
- 重启到 Android / System

### 待验证 / 已知限制

- 振动（Vibrator）—— 未显式配置，待设备验证
- 完整 FBE / 密码矩阵 —— 基础解密可用，全密码矩阵仍在验证中
- USB OTG —— 未充分验证
- 闪光灯（Flashlight）—— 当前关闭（OF_FLASHLIGHT_ENABLE=0），待验证安全 LED 节点后开启
- 解密后主题重载 —— 已通过 `OF_NO_RELOAD_AFTER_DECRYPTION=1` 规避（参考同类设备修复）

> 上述「已配置 / 预期可用」基于构建配置与同平台参考设备经验；真机运行时验证（runtime
> validation）仍在进行中。

### 解密相关说明

vivo 的 FBE 加密存在特殊约束，设备树已做如下处理以免损坏用户密钥：

- `device.mk` 中将 `PLATFORM_SECURITY_PATCH` / `PLATFORM_VENDOR_SECURITY_PATCH` 对齐为
  `2026-06-01`，使 keymint 上报的补丁级别与设备 keyblob 一致，否则会触发
  `REQUIRES_UPGRADE` 并写入「毒药」标记，导致升级半完成。
- `vendorsetup.sh` 中设置了：
  - `OF_FBE_METADATA_MOUNT_IGNORE=1`
  - `OF_DONT_PATCH_ENCRYPTED_DEVICE=1`
  - `OF_NO_RELOAD_AFTER_DECRYPTION=1`

> ⚠️ 注意：`userdata` 分区为加密 / 映射设备，整块 `dd` 读出为乱码、写入会丢光用户数据。
> 备份 / 救砖方案**不要**把 userdata 当普通块分区处理，取用户数据应走文件系统层或 TWRP 的 tar 备份。

## AVB 策略

| 项目 | 策略 |
| --- | --- |
| Recovery fstab AVB 挂载校验 | 关闭（依赖 vendor_boot 结构校验） |
| vendor_boot 结构 AVB footer | 保留（KEEP） |
| vbmeta 自动改写 / patch | 开启（FOX_PATCH_VBMETA_FLAG=1，--flags 3） |

`BoardConfig.mk` 中 `BOARD_AVB_ENABLE := true`，并叠加
`BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --flags 3`。该策略不解锁 Bootloader，也不改动
Boot ROM / LK 的验证策略，仅处理 recovery 侧的 AVB 表现。

## 验证状态

本设备树为基于预编译内核 / dtb / dtbo / vendor_ramdisk 的移植体：构建期配置已就绪，
但真机独立复现与完整运行时验证（触控、FBE 全矩阵、OTG、振动等）仍在进行中。请以
实际刷机测试为准。

## 目录结构

```
PD2415/
├── Android.mk                 # 条件包含子目录 Makefile（仅 PD2415）
├── AndroidProducts.mk         # 注册 twrp_PD2415-eng lunch 目标
├── BoardConfig.mk             # 板级配置（架构 / 分区 / 内核 / AVB / TWRP / OrangeFox）
├── twrp_PD2415.mk             # 产品定义（PRODUCT_DEVICE / NAME / 继承 common.mk）
├── device.mk                  # A/B、动态分区、HAL、OTA 证书等包配置
├── vendorsetup.sh             # OrangeFox 构建环境变量导出
├── system.prop                # 系统属性（fstab 前缀 / USB / crypto / TEE / 时区）
├── bootctrl/                  # boot_control HAL（MTK UFS 实现，含 recovery 变体）
├── init/                      # libinit_PD2415（设备初始化库）
├── mtk_plpath_utils/          # MTK 分区路径工具（postinstall）
├── recovery/                  # TWRP/OrangeFox recovery 资源与库
├── security/                  # vivo_ota 签名证书（OTA 校验）
└── prebuilt/                  # 预编译产物
    ├── kernel                 # 预编译内核（OF_FORCE_PREBUILT_KERNEL）
    ├── dtbo.img               # 独立 DTBO
    ├── dtb/                   # vendor_boot 的 dtb 段（不能由 dtbo.img 替代）
    └── vendor_ramdisk/        # 由官方 vendor_boot 解包转换的 ramdisk
```

### 关于 vendor_boot 的关键约束

`BoardConfig.mk` 中 `BOARD_PREBUILT_DTBIMAGE_DIR` 指向 `prebuilt/dtb`，这是官方
`vendor_boot.img` 解包得到的 **DTB 容器**，与 `dtbo.img` 里的条目内容完全不同，二者
**不能互相替代**。不设置 `BOARD_INCLUDE_DTB_IN_BOOTIMG` 时 `mkbootimg` 不带 `--dtb`，
产物 dtb 长度为 0，LK 无法选板。

recovery 通过 `BOARD_INCLUDE_RECOVERY_RAMDISK_IN_VENDOR_BOOT := true` 打包进
`vendor_boot`（vAB 设备 recovery 的正确载体）；`prebuilt/vendor_ramdisk/` 提供由官方
vendor_boot 转换而来的 ramdisk 内容。

## 构建

### 1. 获取源码（OrangeFox 同步脚本，基于 TWRP 14.1 分支）

```bash
mkdir -p ~/fox_14.1
cd ~/fox_14.1
git clone https://gitlab.com/OrangeFox/sync.git
cd sync
./orangefox_sync.sh --branch 14.1 --path ~/fox_14.1
```

### 2. 克隆本设备树

设备路径必须与 `BoardConfig.mk` 中的 `DEVICE_PATH := device/VIVO/PD2415` 一致：

```bash
cd ~/fox_14.1/device
mkdir -p VIVO
cd VIVO
git clone <本仓库地址> PD2415
```

### 3. 编译

```bash
cd ~/fox_14.1
source build/envsetup.sh
lunch twrp_PD2415-eng
mka recoveryimage
```

产物 recovery 镜像通常位于 `out/target/product/PD2415/recovery.img`。由于 PD2415 / PD2419
共用固件、PD2458 同属 X200 系列，一份构建产物（以主代号 `PD2415` 命名）可直接用于这三款
机型，无需单独为 PD2419 / PD2458 切换 lunch 目标。

## 刷入

设备为虚拟 A/B 设备，recovery 位于 `vendor_boot`。进入 fastboot 后，刷写**完整**镜像到
`vendor_boot` 分区（产物即含 recovery 的 vendor_boot，不要只刷 standalone 片段）：

```bash
fastboot flash vendor_boot out/target/product/PD2415/recovery.img
fastboot reboot recovery
```

注意事项：

- **保留另一个 slot 不动**：初测阶段只刷当前 slot，切勿双 slot 都刷。
- **回退 stock**：若首次系统启动测试失败，回到 bootloader 刷回对应的官方
  `vendor_boot` 镜像再重启：`fastboot flash vendor_boot <官方 vendor_boot.img> && fastboot reboot`。
- 刷机前请确认已解锁 Bootloader 并已备份重要数据。

## 许可证

遵循 Apache License 2.0 与 GPL 相关条款（详见各源文件头注释）。
