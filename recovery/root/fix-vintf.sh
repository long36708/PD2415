#!/system/bin/sh
# fix-vintf.sh — 修正 recovery ramdisk 的 VINTF 声明, 让 servicemanager 放行 AIDL 注册
#
# 背景 (设备已验证):
# 1) 构建源码生成的 /system/etc/vintf/manifest.xml 是 version 9.0 (Android 15 格式,
#    含 max-level 属性), 但 recovery ramdisk 里的 libvintf.so 是平台 8.0 变体,
#    解析报 "Unrecognized manifest.version 9.0 (libvintf@8.0)" -> framework
#    manifest 整体 NULL -> servicemanager 把所有 AIDL addService 判为 undeclared
#    拒绝 (-3), keystore2 每 5s panic 重启, health/boot HAL 同样 abort.
# 2) 构建把 boot-service.mtk / health-service.example 两个 type="device" 的
#    fragment 放进了 framework 目录 /system/etc/vintf/manifest/, libvintf 合并
#    时 "Cannot add a device manifest to a framework manifest" -> 同样 NULL.
#    官方 platform.rc 的做法是 copy 到 /vendor/etc/vintf/manifest/ 再 rm,
#    但那份 rc 在平台片段里, recovery 不 import, 这里等效复刻.
#
# 必须在 servicemanager (class core, on boot) 启动前完成: 挂在 on early-init.
BB=/system/bin/busybox

# 1) 9.0 -> 8.0, 去掉 8.0 不认识的 max-level 属性
for f in /system/etc/vintf/manifest.xml \
         /system/etc/vintf/manifest/*.xml \
         /vendor/etc/vintf/manifest.xml \
         /vendor/etc/vintf/manifest/*.xml; do
    [ -f "$f" ] || continue
    $BB sed -i -e 's/version="9.0"/version="8.0"/g' \
               -e 's/ max-level="[0-9]*"//g' "$f"
done

# 2) device-type fragment 从 framework 目录挪到 vendor 目录
mkdir -p /vendor/etc/vintf/manifest
for f in /system/etc/vintf/manifest/*.xml; do
    [ -f "$f" ] || continue
    if $BB grep -q 'type="device"' "$f"; then
        mv "$f" /vendor/etc/vintf/manifest/
    fi
done
