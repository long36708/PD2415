#!/system/bin/sh
# PD2415 fastbootd 一键入口 (2026-10-27)
#
# 为什么需要这个脚本: 平台 hw/init.rc 没有 AOSP 的 "on fastbootd" 块, 直接
# setprop sys.usb.config fastboot 会踩三个坑:
#   1. adbd 还在跑, UDC 解绑后它死亡重启, ffs.ready 边沿重放全部 gadget
#      action, 与 fastbootd 自己的绑定互相踩踏;
#   2. fastbootd 要等 BootControl/Health HAL 就绪后才打开 ffs ep0 并置
#      ffs.ready=1, 过早绑定 UDC 枚举不出设备;
#   3. 三套 rc (init.recovery.usb.rc / init.recovery.mt6991.rc / hw/init.rc)
#      的 action 在同一状态各写一遍 —— PID 已在 usb.rc 里对齐为幂等, 但
#      none 清场仍是必要的第一步。
#
# 用法: adb shell enter_fastbootd.sh
#   (脚本自动后台化 —— config=none 会立刻断开 adb, 主流程必须在分离进程中跑)
# 日志: /data/local/tmp/fastbootd_entry.log
#       /data/local/tmp/usbdebug.log  (观测器, USB 死了日志也活着, 重启后可读)
# 退出 fastbootd: PC 端 `fastboot reboot-recovery`
# 刷逻辑分区 (system_b 等) 前先在 OrangeFox 里卸载对应分区, 否则 fastbootd
# 打开被 recovery dm 占用的分区会报 Could not open partition。

LOG=/data/local/tmp/fastbootd_entry.log

if [ "$1" != "--bg" ]; then
    nohup "$0" --bg >"$LOG" 2>&1 &
    echo "fastbootd entry detached; log: $LOG"
    exit 0
fi

# 观测器: 每 2s 采样 USB/内核状态到 /data, 最多 10 分钟 (USB 死了日志仍持续)
(
    n=0
    while [ "$n" -lt 300 ]; do
        {
            date
            echo "-- props"; getprop | grep -E 'sys\.usb|ffs|fastbootd'
            echo "-- gadget"; cat /config/usb_gadget/g1/idProduct /config/usb_gadget/g1/UDC 2>/dev/null
            echo "-- dmesg tail"; dmesg | tail -30
        } >> /data/local/tmp/usbdebug.log 2>&1
        sleep 2; n=$((n+1))
    done
) &

# 1) none 清场: 停 adbd / 解绑 UDC / 清符号链接 (adb 至此断开)
setprop sys.usb.config none
sleep 2

# 2) 启动 fastbootd, 等它完成 HAL 初始化并打开 ffs ep0 (置 ffs.ready=1)
[ "$(getprop init.svc.fastbootd)" = running ] || start fastbootd
n=0
while [ "$(getprop sys.usb.ffs.ready)" != "1" ] && [ "$n" -lt 45 ]; do
    sleep 1; n=$((n+1))
done
echo "ffs.ready=$(getprop sys.usb.ffs.ready) wait=${n}s svc=$(getprop init.svc.fastbootd)"

# 3) 切 fastboot: 触发 gadget 绑定 (PID 已在 usb.rc 对齐, action 幂等)
setprop sys.usb.config fastboot
sleep 3
echo "entry done: config=$(getprop sys.usb.config) ffs.ready=$(getprop sys.usb.ffs.ready)"
echo "PC 端 fastboot devices 应可见 (PID 18d1:4ee0); 退出用 fastboot reboot-recovery"
