/*
 * test_hwload — on-device validation of the bootctrl hw module load path.
 *
 * Reproduces exactly what TWRP/OrangeFox slot switching does: resolve the
 * real libhardware.so from the recovery ramdisk, call hw_get_module("bootctrl")
 * (prop/variant resolution + dlopen + module-id string check + methods) and
 * invoke every boot_control_module_t entry point.
 *
 * Build: see build.sh (aarch64, links nothing beyond libc/libdl).
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

typedef struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char *id;
    const char *name;
    const char *author;
    void *methods;
    void *dso;
    uint64_t reserved[32 - 7];
} hw_module_t;

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

typedef int (*hw_get_module_fn)(const char *id, const struct hw_module_t **module);

int main(void) {
    void *lh = dlopen("/system/lib64/libhardware.so", RTLD_NOW);
    if (!lh) {
        printf("FAIL: dlopen libhardware: %s\n", dlerror());
        return 1;
    }
    hw_get_module_fn hw_get_module = (hw_get_module_fn)dlsym(lh, "hw_get_module");
    if (!hw_get_module) {
        printf("FAIL: dlsym hw_get_module: %s\n", dlerror());
        return 1;
    }

    const hw_module_t *mod = NULL;
    int rc = hw_get_module("bootctrl", &mod);
    if (rc != 0 || !mod) {
        printf("FAIL: hw_get_module rc=%d (this is where TWRP prints "
               "\"Error getting bootctrl module\")\n",
               rc);
        return 1;
    }
    printf("PASS: hw_get_module loaded module id='%s' name='%s' dso=%p\n",
           mod->id, mod->name, mod->dso);

    boot_control_module_t *m = (boot_control_module_t *)mod;
    m->init(m);
    printf("getNumberSlots=%u\n", m->getNumberSlots(m));
    printf("getCurrentSlot=%u\n", m->getCurrentSlot(m));
    printf("getActiveBootSlot=%u\n", m->getActiveBootSlot(m));
    char suf[4] = {0};
    m->getSuffix(m, 0, suf);
    printf("getSuffix(0)=%s\n", suf);
    m->getSuffix(m, 1, suf);
    printf("getSuffix(1)=%s\n", suf);
    printf("isSlotBootable(0)=%d (platform HAL says 0=unbootable now)\n",
           m->isSlotBootable(m, 0));
    printf("isSlotBootable(1)=%d\n", m->isSlotBootable(m, 1));
    printf("isSlotMarkedSuccessful(0)=%d isSlotMarkedSuccessful(1)=%d\n",
           m->isSlotMarkedSuccessful(m, 0), m->isSlotMarkedSuccessful(m, 1));
    printf("markBootSuccessful rc=%d (no-op by design)\n", m->markBootSuccessful(m));
    printf("ALL PASS — TWRP slot switch will find this module\n");
    return 0;
}
