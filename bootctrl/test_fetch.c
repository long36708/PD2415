/*
 * test_fetch — verify the HIDL passthrough fetch path that OrangeFox slot
 * switching actually uses (IBootControl::getService -> passthrough ->
 * dlopen("android.hardware.boot@1.0-impl.so") -> HIDL_FETCH_IBootControl).
 *
 * Loads the impl under its EXACT passthrough lookup name and checks that
 * HIDL_FETCH_IBootControl initializes successfully against the live misc
 * partition (format-compatible = non-null return; format-mismatch would
 * make BootControl::Init() fail and return null).
 *
 * Read-only: init neither writes misc (CRC-valid content takes the load
 * path, not the auto-init path) nor calls any mutating HAL method. The
 * caller should still diff misc[2048:2080] before/after as a tripwire.
 */

#include <dlfcn.h>
#include <stdio.h>

typedef void* (*fetch_fn)(const char*);

int main(void) {
    const char* path = "/system/lib64/hw/android.hardware.boot@1.0-impl.so";
    void* h = dlopen(path, RTLD_NOW);
    if (!h) {
        printf("FAIL dlopen: %s\n", dlerror());
        return 1;
    }
    fetch_fn fetch = (fetch_fn)dlsym(h, "HIDL_FETCH_IBootControl");
    if (!fetch) {
        printf("FAIL dlsym HIDL_FETCH_IBootControl: %s\n", dlerror());
        return 1;
    }
    void* svc = fetch("default");
    if (!svc) {
        printf("FAIL HIDL_FETCH returned null — BootControl::Init rejected the misc boot_ctrl block\n");
        return 2;
    }
    printf("PASS HIDL_FETCH_IBootControl('default') = %p — passthrough service obtainable\n", svc);
    return 0;
}
