/*
 * kernmem.c
 *
 * Estimate Linux kernel memory usage.
 *
 * - Static ELF sections (.text, .data, .bss) from:
 *      /boot/System.map-$(uname -r)
 *   falling back to:
 *      /proc/kallsyms
 *
 * - Dynamic kernel memory from /proc/meminfo:
 *      Slab, PageTables, VmallocUsed, KernelStack
 *
 * - Module memory from /proc/modules
 *
 * Build:
 *     gcc -O2 -o kernmem kernelmem3.c
 *
 * Run:
 *     ./kernmem
 *
 * Author: Jerry Richardson <jerry@jerryslab.com>
 * Copyright (C) 2025
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#define KALLSYMS_PATH "/proc/kallsyms"
#define MEMINFO_PATH  "/proc/meminfo"
#define MODULES_PATH  "/proc/modules"

/* ---------- Symbol lookup helpers ---------- */

static unsigned long long get_symbol_from_file(FILE *f, const char *name) {
    char sym_name[256];
    char type;
    unsigned long long addr;

    rewind(f);
    while (fscanf(f, "%llx %c %255s", &addr, &type, sym_name) == 3) {
        if (strcmp(sym_name, name) == 0) {
            return addr;
        }
    }
    return 0;
}

static unsigned long long get_symbol_from_path(const char *path, const char *name) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    unsigned long long addr = get_symbol_from_file(f, name);
    fclose(f);
    return addr;
}

/* Read .text/.data/.bss sizes in kB using System.map, fallback to kallsyms */
static int get_static_sections_kb(unsigned long long *text_kb,
                                  unsigned long long *data_kb,
                                  unsigned long long *bss_kb) {
    struct utsname uts;
    char sysmap_path[512];

    *text_kb = *data_kb = *bss_kb = 0;

    if (uname(&uts) == 0) {
        snprintf(sysmap_path, sizeof(sysmap_path),
                 "/boot/System.map-%s", uts.release);
    } else {
        sysmap_path[0] = '\0';
    }

    unsigned long long _text = 0, _etext = 0;
    unsigned long long _sdata = 0, _edata = 0;
    unsigned long long _bss_start = 0, _bss_stop = 0;

    /* 1) Try System.map-<release> */
    if (sysmap_path[0]) {
        FILE *f = fopen(sysmap_path, "r");
        if (f) {
            char sym_name[256];
            char type;
            unsigned long long addr;

            while (fscanf(f, "%llx %c %255s", &addr, &type, sym_name) == 3) {
                if (strcmp(sym_name, "_text") == 0)         _text = addr;
                else if (strcmp(sym_name, "_etext") == 0)   _etext = addr;
                else if (strcmp(sym_name, "_sdata") == 0)   _sdata = addr;
                else if (strcmp(sym_name, "_edata") == 0)   _edata = addr;
                else if (strcmp(sym_name, "__bss_start") == 0) _bss_start = addr;
                else if (strcmp(sym_name, "__bss_stop") == 0)  _bss_stop = addr;
            }
            fclose(f);
        }
    }

    /* If we didn't find them in System.map, try kallsyms */
    if (_text == 0 || _etext == 0 || _sdata == 0 || _edata == 0 ||
        _bss_start == 0 || _bss_stop == 0) {

        _text      = get_symbol_from_path(KALLSYMS_PATH, "_text");
        _etext     = get_symbol_from_path(KALLSYMS_PATH, "_etext");
        _sdata     = get_symbol_from_path(KALLSYMS_PATH, "_sdata");
        _edata     = get_symbol_from_path(KALLSYMS_PATH, "_edata");
        _bss_start = get_symbol_from_path(KALLSYMS_PATH, "__bss_start");
        _bss_stop  = get_symbol_from_path(KALLSYMS_PATH, "__bss_stop");
    }

    if (_text == 0 || _etext == 0 ||
        _sdata == 0 || _edata == 0 ||
        _bss_start == 0 || _bss_stop == 0) {
        /* Can't get reliable static section sizes */
        return -1;
    }

    *text_kb = (_etext - _text) / 1024;
    *data_kb = (_edata - _sdata) / 1024;
    *bss_kb  = (_bss_stop - _bss_start) / 1024;
    return 0;
}

/* ---------- /proc helpers ---------- */

static long read_meminfo_kb(const char *key) {
    FILE *f = fopen(MEMINFO_PATH, "r");
    if (!f) return -1;

    char line[256];
    long val = -1;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, strlen(key)) == 0) {
            sscanf(line + strlen(key), " %ld kB", &val);
            break;
        }
    }

    fclose(f);
    return val;
}

static long read_modules_kb(void) {
    FILE *f = fopen(MODULES_PATH, "r");
    if (!f) return -1;

    char name[256];
    long size;
    long total = 0;

    while (fscanf(f, "%255s %ld", name, &size) == 2) {
        total += size;             /* size is in bytes */
        fgets(name, 255, f);       /* consume rest of line */
    }

    fclose(f);
    return total / 1024; /* bytes -> kB */
}

/* ---------- Main ---------- */

int main(void) {
    unsigned long long text_kb = 0, data_kb = 0, bss_kb = 0;
    int static_ok = get_static_sections_kb(&text_kb, &data_kb, &bss_kb);

    long slab_kb       = read_meminfo_kb("Slab:");
    long pagetables_kb = read_meminfo_kb("PageTables:");
    long vmalloc_kb    = read_meminfo_kb("VmallocUsed:");
    long kstack_kb     = read_meminfo_kb("KernelStack:");
    long modules_kb    = read_modules_kb();

    long static_total_kb  = 0;
    long dynamic_total_kb = 0;
    long grand_total_kb   = 0;

    if (static_ok == 0) {
        static_total_kb = (long)(text_kb + data_kb + bss_kb);
    }

    if (slab_kb       > 0) dynamic_total_kb += slab_kb;
    if (pagetables_kb > 0) dynamic_total_kb += pagetables_kb;
    if (vmalloc_kb    > 0) dynamic_total_kb += vmalloc_kb;
    if (kstack_kb     > 0) dynamic_total_kb += kstack_kb;

    grand_total_kb = static_total_kb + dynamic_total_kb;
    if (modules_kb > 0)
        grand_total_kb += modules_kb;

    printf("========== Linux Kernel Memory Usage (kernmem) ==========\n\n");

    if (static_ok == 0) {
        printf("Static kernel ELF sections (.text/.data/.bss):\n");
        printf("  .text:      %10llu kB\n", text_kb);
        printf("  .data:      %10llu kB\n", data_kb);
        printf("  .bss:       %10llu kB\n", bss_kb);
        printf("  Static total: %8ld kB (%.2f MB)\n\n",
               static_total_kb, static_total_kb / 1024.0);
    } else {
        printf("Static kernel ELF sections: unavailable "
               "(no usable System.map/kallsyms)\n\n");
    }

    printf("Dynamic kernel allocations (/proc/meminfo):\n");
    if (slab_kb >= 0)
        printf("  Slab:        %10ld kB\n", slab_kb);
    if (pagetables_kb >= 0)
        printf("  PageTables:  %10ld kB\n", pagetables_kb);
    if (vmalloc_kb >= 0)
        printf("  VmallocUsed: %10ld kB\n", vmalloc_kb);
    if (kstack_kb >= 0)
        printf("  KernelStack: %10ld kB\n", kstack_kb);
    printf("  Dynamic total: %8ld kB (%.2f MB)\n\n",
           dynamic_total_kb, dynamic_total_kb / 1024.0);

    printf("Module memory (/proc/modules):\n");
    if (modules_kb >= 0)
        printf("  Modules:     %10ld kB (%.2f MB)\n\n",
               modules_kb, modules_kb / 1024.0);
    else
        printf("  Modules:     unavailable (no /proc/modules)\n\n");

    printf("============================================================\n");
    printf("Estimated TOTAL kernel memory: %ld kB (%.2f MB)\n",
           grand_total_kb, grand_total_kb / 1024.0);

    return 0;
}
