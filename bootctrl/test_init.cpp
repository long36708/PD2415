/*
 * test_init — runtime introspection of the mtkimpl's libboot_control.
 *
 * android::bootable::BootControl is exported by the impl library; we
 * construct a zeroed object, run Init() (resolves the misc device via the
 * recovery fstab and loads the boot_ctrl block) and report:
 *   - Init() result
 *   - the misc device string it resolved (ASCII visible in the object)
 *   - GetNumberSlots / GetCurrentSlot / IsSlotBootable
 *   - optionally SetActiveBootSlot(target) with misc snapshotted before
 *     and restored after, to observe the exact byte transform in isolation.
 *
 * Build: see build.sh (needs libc++ for std::string-free usage: we treat
 * the object as raw bytes; Init fills it).
 */

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef bool (*init_fn)(void*);
typedef bool (*set_fn)(void*, unsigned);
typedef unsigned (*getu_fn)(void*);
typedef bool (*getb_fn)(void*, unsigned);

static void dump_misc(const char* label, unsigned char* out32) {
    int fd = open("/dev/block/by-name/misc", O_RDONLY);
    if (fd < 0) { printf("%s: open misc failed\n", label); return; }
    lseek(fd, 2048, SEEK_SET);
    read(fd, out32, 32);
    close(fd);
    printf("%s: ", label);
    for (int i = 0; i < 32; i++) printf("%02x", out32[i]);
    printf("\n");
}

static void restore_misc(const unsigned char* src32) {
    int fd = open("/dev/block/by-name/misc", O_WRONLY);
    if (fd < 0) { printf("restore: open misc WR failed\n"); return; }
    lseek(fd, 2048, SEEK_SET);
    write(fd, src32, 32);
    fsync(fd);
    close(fd);
}

int main(int argc, char** argv) {
    int do_set = (argc > 1 && strcmp(argv[1], "set") == 0);
    unsigned target = (argc > 2) ? (unsigned)atoi(argv[2]) : 0;

    void* h = dlopen("/system/lib64/hw/android.hardware.boot@1.0-impl.so", RTLD_NOW);
    if (!h) { printf("dlopen fail: %s\n", dlerror()); return 1; }
    init_fn Init = (init_fn)dlsym(h, "_ZN7android8bootable11BootControl4InitEv");
    set_fn Set = (set_fn)dlsym(h, "_ZN7android8bootable11BootControl17SetActiveBootSlotEj");
    getu_fn GetNumberSlots = (getu_fn)dlsym(h, "_ZN7android8bootable11BootControl14GetNumberSlotsEv");
    getu_fn GetCurrentSlot = (getu_fn)dlsym(h, "_ZN7android8bootable11BootControl14GetCurrentSlotEv");
    getb_fn IsSlotBootable = (getb_fn)dlsym(h, "_ZN7android8bootable11BootControl14IsSlotBootableEj");
    if (!Init || !Set || !GetNumberSlots) { printf("dlsym fail\n"); return 1; }

    alignas(16) unsigned char obj[512];
    memset(obj, 0, sizeof(obj));
    bool ok = Init(obj);
    printf("Init() = %d\n", ok);
    if (!ok) return 2;

    printf("object[0..0x60]: ");
    for (int i = 0; i < 0x60; i++) {
        if (i % 32 == 0 && i) printf("| ");
        printf("%02x", obj[i]);
    }
    printf("\n");
    printf("object ASCII: ");
    for (int i = 0; i < 0x60; i++)
        putchar((obj[i] >= 0x20 && obj[i] < 0x7f) ? obj[i] : '.');
    printf("\n");

    printf("GetNumberSlots=%u", GetNumberSlots(obj));
    if (GetCurrentSlot) printf(" GetCurrentSlot=%u", GetCurrentSlot(obj));
    if (IsSlotBootable)
        printf(" IsSlotBootable(0)=%d IsSlotBootable(1)=%d",
               IsSlotBootable(obj, 0), IsSlotBootable(obj, 1));
    printf("\n");

    if (!do_set) return 0;

    unsigned char before[32], after[32];
    dump_misc("misc before", before);
    bool rc = Set(obj, target);
    printf("SetActiveBootSlot(%u) = %d\n", target, rc);
    dump_misc("misc after ", after);
    if (memcmp(before, after, 32) != 0) {
        restore_misc(before);
        dump_misc("misc restored", after);
        printf("(snapshot restored — no lasting change)\n");
    } else {
        printf("(misc unchanged)\n");
    }
    return 0;
}
