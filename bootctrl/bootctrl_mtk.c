/*
 * bootctrl.mt6991.so — passthrough boot control HAL (libhardware hw module)
 * for vivo PD2415 / MT6991 (OS6).
 *
 * Why this exists: the platform only ships the AIDL boot service
 * (android.hardware.boot-service.mtk). TWRP/OrangeFox slot switching calls
 * hw_get_module("bootctrl"), which needs a passthrough module named
 * bootctrl.<variant>.so under /vendor/lib64/hw (or odm/system equivalents).
 * No partition of this device contains such a module, hence the recovery UI
 * error "Error getting bootctrl module".
 *
 * On-disk format (32 bytes at misc + 2048), device-verified 2026-08
 * (on-device root analysis: HAL call tracing, LK firmware strings,
 * CRC verification against three independent dumps — see vivo_docs/misc):
 *
 *   +0   char[4]  active slot suffix "_a\0\0" / "_b\0\0"
 *                (authoritative: LK selects the boot slot from this field;
 *                 proven by two device states with identical counters but
 *                 different suffixes booting different slots)
 *   +4   u32      magic 0x42434142 ("BCAB", MTK-custom; AOSP uses 0x42424342)
 *   +8   u8       version (1)
 *   +9   u8       nb_slot (2)
 *   +10  u16      0
 *   +12  u16      slot 0 (A) counter
 *   +14  u16      slot 1 (B) counter
 *                (MTK HAL observed semantics on setActiveBootSlot:
 *                 target = max(a,b), other = target - 1)
 *   +16  12 B     reserved, always 0
 *   +28  u32      CRC-32 (zlib poly 0xEDB88320) over [+0,+28), little-endian
 *
 * Built with the NDK (see build.sh); no dependencies beyond libc so it can
 * be dlopen'ed inside recovery.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* ---- libhardware ABI (subset of hardware/hardware.h + hardware/boot_control.h) ---- */

#define HW_MODULE_TAG 0x48574D54 /* "HWMT" */
#define HAL_MODULE_INFO_SYM HMI  /* libhardware dlsym()s the string "HMI" */

typedef struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version; /* major<<8 | minor */
    uint16_t hal_api_version;
    const char *id;
    const char *name;
    const char *author;
    struct hw_module_methods_t *methods;
    void *dso;
#ifdef __LP64__
    uint64_t reserved[32 - 7];
#else
    uint32_t reserved[32 - 7];
#endif
} hw_module_t;

/* hw_device_t is never constructed: this HAL is consumed through the module
 * struct itself (see bc_open). */
struct hw_device_t;

typedef struct hw_module_methods_t {
    int (*open)(const hw_module_t *module, const char *id,
                struct hw_device_t **device);
} hw_module_methods_t;

typedef struct boot_control_module {
    struct hw_module_t common;
    void (*init)(struct boot_control_module *module);
    unsigned (*getNumberSlots)(struct boot_control_module *module);
    unsigned (*getCurrentSlot)(struct boot_control_module *module);
    int (*markBootSuccessful)(struct boot_control_module *module);
    int (*setActiveBootSlot)(struct boot_control_module *module, unsigned slot);
    int (*setSlotAsUnbootable)(struct boot_control_module *module, unsigned slot);
    int (*isSlotBootable)(struct boot_control_module *module, unsigned slot);
    int (*isSlotMarkedSuccessful)(struct boot_control_module *module, unsigned slot);
    void (*getSuffix)(struct boot_control_module *module, unsigned slot, char *suffix);
    int (*getActiveBootSlot)(struct boot_control_module *module);
    void *reserved[31 - 4];
} boot_control_module_t;

/* ---- MTK boot_ctrl on-disk struct ---- */

#define MISC_PATH "/dev/block/by-name/misc"
#define BOOTCTRL_OFFSET 2048
#define BOOTCTRL_SIZE 32
#define MTK_MAGIC 0x42414342u /* on-disk bytes 42 43 41 42 = "BCAB" (LE u32;
                               * NB: docs citing 0x42434142 have the nibbles
                               * transposed — regression test catches this) */

typedef struct {
    uint8_t raw[BOOTCTRL_SIZE];
} mtk_bootctrl;

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

/* CRC-32 (zlib-compatible: reflected, poly 0xEDB88320, init/xorout 0xFFFFFFFF) */
static uint32_t crc32_le(const uint8_t *data, size_t len) {
    static uint32_t table[256];
    static int table_ready = 0;
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;

    if (!table_ready) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        table_ready = 1;
    }
    for (i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* ---- logging: TWRP recovery log + logcat-free ---- */

static void blogf(const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    FILE *f = fopen("/tmp/recovery.log", "a");
    if (f) {
        fprintf(f, "I:bootctrl.mt6991: %s\n", line);
        fclose(f);
    }
}

/* ---- misc partition I/O ---- */

static int bootctrl_read(mtk_bootctrl *bc) {
    int fd = open(MISC_PATH, O_RDONLY);
    if (fd < 0) {
        blogf("open %s failed: %s", MISC_PATH, strerror(errno));
        return -1;
    }
    ssize_t n = pread(fd, bc->raw, BOOTCTRL_SIZE, BOOTCTRL_OFFSET);
    close(fd);
    if (n != BOOTCTRL_SIZE) {
        blogf("short read (%zd)", n);
        return -1;
    }
    return 0;
}

static int bootctrl_write(const mtk_bootctrl *bc) {
    int fd = open(MISC_PATH, O_WRONLY);
    if (fd < 0) {
        blogf("open %s for write failed: %s", MISC_PATH, strerror(errno));
        return -1;
    }
    ssize_t n = pwrite(fd, bc->raw, BOOTCTRL_SIZE, BOOTCTRL_OFFSET);
    fsync(fd);
    close(fd);
    if (n != BOOTCTRL_SIZE) {
        blogf("short write (%zd)", n);
        return -1;
    }
    return 0;
}

/* Validate magic + CRC + suffix byte. */
static int bootctrl_valid(const mtk_bootctrl *bc) {
    uint32_t magic = (uint32_t)bc->raw[4] | ((uint32_t)bc->raw[5] << 8) |
                     ((uint32_t)bc->raw[6] << 16) | ((uint32_t)bc->raw[7] << 24);
    uint32_t stored = (uint32_t)bc->raw[28] | ((uint32_t)bc->raw[29] << 8) |
                      ((uint32_t)bc->raw[30] << 16) | ((uint32_t)bc->raw[31] << 24);
    if (magic != MTK_MAGIC) {
        blogf("magic mismatch %08x", magic);
        return 0;
    }
    if (crc32_le(bc->raw, 28) != stored) {
        blogf("crc mismatch");
        return 0;
    }
    if (bc->raw[1] != 'a' && bc->raw[1] != 'b') {
        blogf("bad suffix byte %02x", bc->raw[1]);
        return 0;
    }
    return 1;
}

static int slot_of_suffix(const mtk_bootctrl *bc) {
    return bc->raw[1] == 'b' ? 1 : 0;
}

/* Fallback current slot from kernel cmdline property. */
static int prop_current_slot(void) {
    char buf[92] = {0};
    /* declared manually: avoids NDK header deprecation churn; bionic libc symbol */
    extern int __system_property_get(const char *, char *);
    if (__system_property_get("ro.boot.slot_suffix", buf) > 0 && buf[0] == '_' &&
        (buf[1] == 'a' || buf[1] == 'b'))
        return buf[1] == 'b' ? 1 : 0;
    return 0;
}

/* Fill a full 32-byte struct from scratch. counters: cnt[0]=slot A, cnt[1]=slot B. */
static void bootctrl_build(mtk_bootctrl *bc, int active_slot,
                           uint16_t cnt0, uint16_t cnt1) {
    memset(bc->raw, 0, BOOTCTRL_SIZE);
    bc->raw[0] = '_';
    bc->raw[1] = active_slot == 1 ? 'b' : 'a';
    wr32(&bc->raw[4], MTK_MAGIC);
    bc->raw[8] = 1; /* version */
    bc->raw[9] = 2; /* nb_slot */
    wr16(&bc->raw[12], cnt0);
    wr16(&bc->raw[14], cnt1);
    wr32(&bc->raw[28], crc32_le(bc->raw, 28));
}

/*
 * Read current counters. If misc is unreadable/invalid, synthesize from the
 * running slot with counters (2,1). The struct is only persisted when a
 * mutating op is requested, and then always written in full.
 */
static void load_or_synthesize(mtk_bootctrl *bc) {
    if (bootctrl_read(bc) == 0 && bootctrl_valid(bc))
        return;
    blogf("misc invalid — synthesizing baseline from ro.boot.slot_suffix");
    bootctrl_build(bc, prop_current_slot(), 2, 1);
}

/* ---- HAL implementation ---- */

static void bc_init(boot_control_module_t *module) {
    (void)module;
    mtk_bootctrl bc;
    if (bootctrl_read(&bc) == 0 && bootctrl_valid(&bc)) {
        blogf("init ok: suffix=%c a=%u b=%u", bc.raw[1], rd16(&bc.raw[12]),
              rd16(&bc.raw[14]));
    } else {
        blogf("init: misc content invalid (will rebuild on first write)");
    }
}

static unsigned bc_get_number_slots(boot_control_module_t *module) {
    (void)module;
    return 2;
}

static unsigned bc_get_current_slot(boot_control_module_t *module) {
    (void)module;
    mtk_bootctrl bc;
    load_or_synthesize(&bc);
    return (unsigned)slot_of_suffix(&bc);
}

static int bc_mark_boot_successful(boot_control_module_t *module) {
    (void)module;
    /* Do not touch the metadata here: the running slot's successful flag is
     * managed by the system-side boot control, and writing it from recovery
     * would fight a pending switch (suffix already points at the target).
     * Report success unconditionally. */
    return 0;
}

/*
 * Activation transform (mirrors stock libboot_control, per its disasm):
 *   - target slot: bits 0..6 := 0x6f (priority 15, tries 6, successful 0);
 *     successful_boot (bit 7) is deliberately CLEARED — a fresh activation
 *     must keep the bootloader retry path alive so a slot that fails to boot
 *     can fall back instead of looping forever (device-verified 2026-10-25:
 *     an activation carrying successful=1 boot-looped endlessly on the
 *     dm-verity-corrupted slot A). verity_corrupted (bit 8) is preserved.
 *   - other slot: priority 15 demoted to 14 (tries/successful preserved);
 *     unbootable slots stay unbootable.
 * Refuses when the target slot's metadata is zero — that is the platform's
 * own "unbootable" verdict (failed verity / aborted OTA), and activating
 * such a slot boot-loops. force overrides.
 */
static int activation_transform(mtk_bootctrl *bc, unsigned slot, int force,
                                uint16_t *out_a, uint16_t *out_b) {
    uint16_t a = rd16(&bc->raw[12]);
    uint16_t b = rd16(&bc->raw[14]);
    uint16_t tgt_old = slot == 0 ? a : b;
    uint16_t oth_old = slot == 0 ? b : a;
    if (tgt_old == 0 && !force) {
        blogf("activate(%u): REFUSED — slot metadata is zero (platform-marked "
              "unbootable: failed verity/OTA). Use force to override.",
              slot);
        return -1;
    }
    uint16_t tgt = (uint16_t)((tgt_old & 0x0100u) | 0x006fu);
    uint16_t oth = ((oth_old & 0x000fu) == 0x000fu)
                       ? (uint16_t)((oth_old & 0xfff0u) | 0x000eu)
                       : oth_old;
    *out_a = slot == 0 ? tgt : oth;
    *out_b = slot == 1 ? tgt : oth;
    return 0;
}

static int bc_set_active_boot_slot(boot_control_module_t *module, unsigned slot) {
    (void)module;
    if (slot > 1) {
        blogf("setActiveBootSlot: invalid slot %u", slot);
        return -1;
    }
    mtk_bootctrl bc;
    load_or_synthesize(&bc);
    uint16_t na, nb;
    if (activation_transform(&bc, slot, 0, &na, &nb) != 0)
        return -1;
    blogf("setActiveBootSlot(%u): meta a=%04x b=%04x -> a=%04x b=%04x", slot,
          rd16(&bc.raw[12]), rd16(&bc.raw[14]), na, nb);
    bootctrl_build(&bc, (int)slot, na, nb);
    if (bootctrl_write(&bc) != 0)
        return -1;
    /* read-back verification */
    mtk_bootctrl chk;
    if (bootctrl_read(&chk) == 0 && bootctrl_valid(&chk) &&
        memcmp(chk.raw, bc.raw, BOOTCTRL_SIZE) == 0) {
        blogf("setActiveBootSlot(%u) verified", slot);
        return 0;
    }
    blogf("setActiveBootSlot(%u) read-back mismatch!", slot);
    return -1;
}

static int bc_set_slot_as_unbootable(boot_control_module_t *module, unsigned slot) {
    (void)module;
    if (slot > 1) {
        blogf("setSlotAsUnbootable: invalid slot %u", slot);
        return -1;
    }
    mtk_bootctrl bc;
    load_or_synthesize(&bc);
    uint16_t a = rd16(&bc.raw[12]);
    uint16_t b = rd16(&bc.raw[14]);
    if (slot == 0)
        a = 0;
    else
        b = 0;
    /* keep the suffix pointing at a bootable slot when possible */
    int active = slot_of_suffix(&bc);
    if (active == (int)slot)
        active = 1 - (int)slot;
    bootctrl_build(&bc, active, a, b);
    blogf("setSlotAsUnbootable(%u): suffix=%c a=%u b=%u", slot, bc.raw[1], a, b);
    return bootctrl_write(&bc);
}

static int bc_is_slot_bootable(boot_control_module_t *module, unsigned slot) {
    (void)module;
    if (slot > 1)
        return 0;
    mtk_bootctrl bc;
    load_or_synthesize(&bc);
    return rd16(&bc.raw[12 + slot * 2]) > 0;
}

static int bc_is_slot_marked_successful(boot_control_module_t *module, unsigned slot) {
    (void)module;
    if (slot > 1)
        return 0;
    mtk_bootctrl bc;
    load_or_synthesize(&bc);
    return rd16(&bc.raw[12 + slot * 2]) > 0;
}

static void bc_get_suffix(boot_control_module_t *module, unsigned slot, char *suffix) {
    (void)module;
    if (slot > 1) {
        if (suffix)
            suffix[0] = '\0';
        return;
    }
    if (suffix) {
        suffix[0] = '_';
        suffix[1] = slot == 1 ? 'b' : 'a';
        suffix[2] = '\0';
    }
}

static int bc_get_active_boot_slot(boot_control_module_t *module) {
    return (int)bc_get_current_slot(module);
}

static int bc_open(const hw_module_t *module, const char *id, struct hw_device_t **device) {
    (void)module;
    (void)id;
    (void)device;
    /* The boot control HAL is used via the module struct directly
     * (hw_get_module + function pointers); open() is never exercised. */
    return -1;
}

static hw_module_methods_t bc_methods = {
    .open = bc_open,
};

boot_control_module_t HAL_MODULE_INFO_SYM __attribute__((visibility("default"))) = {
    .common =
        {
            .tag = HW_MODULE_TAG,
            .module_api_version = 0x0101, /* 1.1 */
            .hal_api_version = 0,
            .id = "bootctrl", /* libhardware load() string-matches this */
            .name = "MediaTek MT6991 boot control HAL",
            .author = "PD2415 OrangeFox device tree",
            .methods = &bc_methods,
            .dso = NULL,
            .reserved = {0},
        },
    .init = bc_init,
    .getNumberSlots = bc_get_number_slots,
    .getCurrentSlot = bc_get_current_slot,
    .markBootSuccessful = bc_mark_boot_successful,
    .setActiveBootSlot = bc_set_active_boot_slot,
    .setSlotAsUnbootable = bc_set_slot_as_unbootable,
    .isSlotBootable = bc_is_slot_bootable,
    .isSlotMarkedSuccessful = bc_is_slot_marked_successful,
    .getSuffix = bc_get_suffix,
    .getActiveBootSlot = bc_get_active_boot_slot,
    .reserved = {0},
};

#ifdef BOOTCTRL_TOOL
/*
 * do_switch — standalone slot switch for recovery adb shells.
 *
 * Performs the device-verified complete switch in one write: suffix (the
 * field LK actually selects by) + slot counters (target = larger value,
 * other = value - 1, matching the stock HAL's observable arithmetic) +
 * CRC-32, with read-back verification. This is the same transform the
 * BOOTCTRL_HOST_TEST oracle validates byte-for-byte.
 *
 * Usage: do_switch 0|1   (0 = slot A, 1 = slot B); power-cycle to boot it.
 */
int main(int argc, char **argv) {
    int force = (argc > 2 && !strcmp(argv[2], "force"));
    if (argc < 2 || (strcmp(argv[1], "0") && strcmp(argv[1], "1"))) {
        fprintf(stderr,
                "usage: %s 0|1 [force]   (0=slot A, 1=slot B)\n"
                "Refuses a slot whose metadata is zero (platform-marked\n"
                "unbootable, e.g. dm-verity-corrupted); append 'force' to\n"
                "override — a forced slot that fails to boot can loop.\n",
                argv[0]);
        return 2;
    }
    int slot = atoi(argv[1]);
    mtk_bootctrl bc;
    load_or_synthesize(&bc);
    printf("current : ");
    for (int i = 0; i < BOOTCTRL_SIZE; i++) printf("%02x", bc.raw[i]);
    printf("  (suffix=%c)\n", bc.raw[1]);
    uint16_t na, nb;
    if (activation_transform(&bc, (unsigned)slot, force, &na, &nb) != 0) {
        fprintf(stderr,
                "REFUSED: slot %d is platform-marked unbootable (metadata 0).\n"
                "Boot it only after re-flashing that slot's partitions.\n",
                slot);
        return 3;
    }
    bootctrl_build(&bc, slot, na, nb);
    printf("target%d : ", slot);
    for (int i = 0; i < BOOTCTRL_SIZE; i++) printf("%02x", bc.raw[i]);
    printf("  (suffix=%c)\n", bc.raw[1]);
    if (bootctrl_write(&bc) != 0) {
        fprintf(stderr, "FAIL: write\n");
        return 1;
    }
    mtk_bootctrl chk;
    if (bootctrl_read(&chk) != 0 || !bootctrl_valid(&chk) ||
        memcmp(chk.raw, bc.raw, BOOTCTRL_SIZE) != 0) {
        fprintf(stderr, "FAIL: read-back mismatch — check misc before reboot!\n");
        return 1;
    }
    printf("OK: verified (tries=6 pending-success: if it fails to boot, the "
           "bootloader can fall back). Reboot to boot slot %c.\n",
           slot ? 'B' : 'A');
    return 0;
}

#elif defined(BOOTCTRL_HOST_TEST)
/*
 * Host-side regression test against device-verified data.
 * Input oracle: the 2026-08-13 pre-switch backup (suffix _b, metadata
 * A=0xef B=0xee, CRC valid — captured on device before a platform-HAL
 * switch). The activation transform must yield suffix _a with target
 * metadata 0x6f (priority 15 / tries 6 / successful 0 — retry semantics)
 * and the other slot unchanged, plus CRC:
 *   5f610000 42434142 01020000 6f00ee00 00000000 00000000 acf6e341
 * Also verifies the unbootable-refusal rule (zero-metadata target).
 */
#include <assert.h>

static void hexdump(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        printf("%02x", p[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <misc-image>\n", argv[0]);
        return 2;
    }
    /* The host build compiles the real I/O against MISC_PATH; we simulate by
     * copying the image over a temp file mounted at that path is not possible,
     * so instead run the transform in-memory via bootctrl_build + crc checks. */
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("open image");
        return 2;
    }
    mtk_bootctrl bc;
    memset(&bc, 0, sizeof(bc));
    size_t n = fread(bc.raw, 1, BOOTCTRL_SIZE, f);
    fclose(f);
    if (n != BOOTCTRL_SIZE) {
        fprintf(stderr, "image too small: %zu\n", n);
        return 2;
    }
    printf("input:  ");
    hexdump(bc.raw, BOOTCTRL_SIZE);
    if (!bootctrl_valid(&bc)) {
        fprintf(stderr, "FAIL: input not a valid boot_ctrl block\n");
        return 1;
    }
    /* replay the 2026-08-13 device switch input through the current
     * activation semantics (target gets 0x6f: priority 15 / tries 6 /
     * successful 0; other slot keeps its non-max metadata) */
    uint16_t na, nb;
    if (activation_transform(&bc, 0, 0, &na, &nb) != 0) {
        fprintf(stderr, "FAIL: transform refused a valid bootable target\n");
        return 1;
    }
    bootctrl_build(&bc, 0, na, nb);
    printf("output: ");
    hexdump(bc.raw, BOOTCTRL_SIZE);
    static const uint8_t expected[BOOTCTRL_SIZE] = {
        0x5f, 0x61, 0x00, 0x00, 0x42, 0x43, 0x41, 0x42, 0x01, 0x02, 0x00, 0x00,
        0x6f, 0x00, 0xee, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0x41, 0xe3, 0xf6, 0xac};
    if (memcmp(bc.raw, expected, BOOTCTRL_SIZE) != 0) {
        fprintf(stderr, "FAIL: transform does not match expected bytes\n");
        return 1;
    }
    /* refusal oracle: a zero-metadata target must be refused without force */
    mtk_bootctrl bad;
    memset(&bad.raw, 0, BOOTCTRL_SIZE);
    bad.raw[0] = '_';
    bad.raw[1] = 'b';
    wr32(&bad.raw[4], MTK_MAGIC);
    bad.raw[9] = 2;
    wr16(&bad.raw[12], 0x0000); /* slot A platform-marked unbootable */
    wr16(&bad.raw[14], 0x009f);
    wr32(&bad.raw[28], crc32_le(bad.raw, 28));
    if (activation_transform(&bad, 0, 0, &na, &nb) == 0) {
        fprintf(stderr, "FAIL: unbootable target was not refused\n");
        return 1;
    }
    if (activation_transform(&bad, 0, 1, &na, &nb) != 0) {
        fprintf(stderr, "FAIL: force override did not work\n");
        return 1;
    }
    printf("PASS: activation transform + unbootable refusal verified\n");
    return 0;
}
#endif /* BOOTCTRL_HOST_TEST */
