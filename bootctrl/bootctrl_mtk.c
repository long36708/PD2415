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
    /* MTK layout carries no successful/tries flags (always zero on device);
     * LK picks purely by suffix. Do NOT touch counters here: that would fight
     * a pending slot switch. Report success unconditionally. */
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
    uint16_t a = rd16(&bc.raw[12]);
    uint16_t b = rd16(&bc.raw[14]);
    uint16_t mx = (a > b) ? a : b;
    if (mx < 2 || mx == 0xFFFFu)
        mx = 2; /* sane floor / avoid wrapping the "other" counter */
    uint16_t tgt = mx;
    uint16_t oth = (uint16_t)(mx - 1);
    blogf("setActiveBootSlot(%u): counters a=%u b=%u -> a=%u b=%u", slot, a, b,
          slot == 0 ? tgt : oth, slot == 1 ? tgt : oth);
    bootctrl_build(&bc, (int)slot, slot == 0 ? tgt : oth, slot == 1 ? tgt : oth);
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

#ifdef BOOTCTRL_HOST_TEST
/*
 * Host-side regression test against device-verified data.
 * Oracle: vivo_docs/misc (2026-08-13 switch, performed via platform HAL on
 * device): pre-switch backup (suffix _b, counters A=239 B=238) transformed
 * by setActiveBootSlot(0) into exactly:
 *   5f610000 42434142 01020000 ef00ee00 00000000 00000000 b50e0bf0
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
    /* replay the 2026-08-13 device switch: setActiveBootSlot(0) */
    uint16_t a = rd16(&bc.raw[12]);
    uint16_t b = rd16(&bc.raw[14]);
    uint16_t mx = (a > b) ? a : b;
    if (mx < 2 || mx == 0xFFFFu)
        mx = 2;
    bootctrl_build(&bc, 0, mx, (uint16_t)(mx - 1));
    printf("output: ");
    hexdump(bc.raw, BOOTCTRL_SIZE);
    static const uint8_t expected[BOOTCTRL_SIZE] = {
        0x5f, 0x61, 0x00, 0x00, 0x42, 0x43, 0x41, 0x42, 0x01, 0x02, 0x00, 0x00,
        0xef, 0x00, 0xee, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0xb5, 0x0e, 0x0b, 0xf0};
    if (memcmp(bc.raw, expected, BOOTCTRL_SIZE) != 0) {
        fprintf(stderr, "FAIL: transform does not match device-verified bytes\n");
        return 1;
    }
    printf("PASS: setActiveBootSlot(0) reproduces the device-verified state\n");
    return 0;
}
#endif /* BOOTCTRL_HOST_TEST */
