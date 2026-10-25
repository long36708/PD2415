#!/bin/sh
# Rebuild bootctrl.mt6991.so (passthrough boot control HAL for recovery).
#
# The shipped artifact is committed at recovery/root/vendor/lib64/hw/
# bootctrl.mt6991.so and lands in the ramdisk /vendor/lib64/hw/ where
# libhardware's hw_get_module("bootctrl") resolves it (ro.hardware=mt6991,
# optionally pinned by ro.hardware.bootctrl=mt6991 in system.prop).
#
# It is shipped as a prebuilt on purpose: this exact NDK build is the one
# device-validated end-to-end (regression test below + test_hwload.c against
# the live recovery). Rebuilding produces a functionally identical module;
# after any source change re-run BOTH validations and refresh the committed
# binary together with the source.
#
# Prereq: NDK (tested with 29.0.14206865). Run from this directory.

set -e
NDK=${NDK:-F:/Android/ndk/29.0.14206865}
CC=$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe

"$CC" --target=aarch64-linux-android29 -O2 -Wall -Wextra -fPIC -shared \
    -o bootctrl.mt6991.so bootctrl_mtk.c -Wl,-soname,bootctrl.mt6991.so

# Device-side regression test: must reproduce the device-verified 2026-08-13
# slot switch byte-for-byte (oracle bytes inside BOOTCTRL_HOST_TEST main).
"$CC" --target=aarch64-linux-android29 -O2 -Wall -Wextra \
    -DBOOTCTRL_HOST_TEST -o bootctrl_test bootctrl_mtk.c

# Loader-path test exercising hw_get_module("bootctrl") through the real
# recovery libhardware.so (push both + a 32-byte boot_ctrl image, then:
#   /tmp/bootctrl_test /tmp/bootctrl_backup.bin
#   /tmp/test_hwload )
"$CC" --target=aarch64-linux-android29 -O2 -Wall -Wextra \
    -o test_hwload test_hwload.c

# Standalone slot switcher for recovery adb (suffix + counters + CRC in one
# verified write — same transform as the module's setActiveBootSlot):
#   /tmp/do_switch 0|1
"$CC" --target=aarch64-linux-android29 -O2 -Wall \
    -DBOOTCTRL_TOOL -o do_switch bootctrl_mtk.c

# HIDL-passthrough introspection (call the mtkimpl's libboot_control
# directly: Init / SetActiveBootSlot with snapshot+restore):
"$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe" \
    --target=aarch64-linux-android29 -O2 -Wall -static-libstdc++ \
    -o test_init test_init.cpp

cp -f bootctrl.mt6991.so ../recovery/root/vendor/lib64/hw/bootctrl.mt6991.so
echo "done — commit recovery/root/vendor/lib64/hw/bootctrl.mt6991.so with your change"
