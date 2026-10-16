#!/system/bin/sh
# Clear the bootloader control block "convert" flag after a successful boot.
# TWRP writes this flag when it flashes a vendor_boot that was built with a
# different boot control layout, so the bootloader knows to re-read the slot
# metadata on the next boot.

MISC=/dev/block/by-name/misc

if [ ! -b "$MISC" ]; then
    echo "bcb_clear: $MISC not found" > /dev/kmsg
    exit 0
fi

# Byte offset of bootloader_control::reserved1[0] within the misc partition:
#   struct bootloader_message  (2048 bytes: cmd[32] + status[32] +
#                               recovery[768] + stage[32] + reserved[1184])
#   slot_suffix[32]
#   magic[4] + version[1] + flags[1] + reserved0[1]
#   slot_info[4] * 2 bytes = 8
#   reserved1[8]
# => 2048 + 32 + 4 + 1 + 1 + 1 + 8 = 2095
OFFSET=2095

# Only touch the byte if the boot control block is present and valid, so we
# never scribble on a misc partition that holds something else.
MAGIC=$(dd if="$MISC" bs=1 skip=2084 count=4 2>/dev/null | od -An -tx1 | tr -d ' \n')
if [ "$MAGIC" != "42414342" ]; then
    echo "bcb_clear: no valid bootloader_control block" > /dev/kmsg
    exit 0
fi

if [ "$(dd if="$MISC" bs=1 skip=$OFFSET count=1 2>/dev/null | od -An -tu1 | tr -d ' ')" = "1" ]; then
    printf '\000' | dd of="$MISC" bs=1 seek=$OFFSET count=1 conv=notrunc 2>/dev/null
    echo "bcb_clear: cleared avb boot control convert flag" > /dev/kmsg
fi

exit 0
