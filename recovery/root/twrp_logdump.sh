#!/system/bin/sh
# Dump the last kernel log to a file on /data so TWRP bug reports can include
# what happened just before the recovery started.

OUT=/data/twrp_last_kmsg
LOG=/sys/fs/pstore/console-ramoops-0

if [ ! -r "$LOG" ]; then
    LOG=/proc/last_kmsg
fi

if [ -r "$LOG" ]; then
    cp "$LOG" "$OUT" 2>/dev/null
    chmod 0644 "$OUT" 2>/dev/null
fi

exit 0
