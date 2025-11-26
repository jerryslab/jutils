/*
 * swapout.c
 *
 * Force a process's memory to be pushed into swap by constraining it
 * to a small-memory cgroup, then restoring the limit afterwards.
 *
 * Requires root (or sufficient privileges to manage cgroups and move PIDs).
 *
 * Usage:
 *   swapout PID [options]
 *
 * Options:
 *   -m, --limit-mb MB       Memory limit during swapout (default: 8 MB)
 *   -r, --target-rss-kb KB  Target RSS to reach before stopping (default: 16384 kB)
 *   -i, --interval SECS     Poll interval in seconds (default: 1.0)
 *   -n, --max-iter N        Maximum iterations before giving up (default: 60)
 *   -q, --quiet             Less verbose output
 *   -h, --help              Show this help
 *
 * Author: Jerry Richardson <jerry@jerryslab.com>
 * Copyright (C) 2025
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <getopt.h>
#include <sys/stat.h>
#include <time.h>

typedef enum {
    CGROUP_NONE = 0,
    CGROUP_V1   = 1,
    CGROUP_V2   = 2
} cgroup_version_t;

typedef struct {
    pid_t pid;
    long rss_kb;
    long swap_kb;
} proc_meminfo_t;

/* ---------- utility helpers ---------- */

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s PID [options]\n"
        "\n"
        "Force a process's memory to be pushed into swap by constraining it to a\n"
        "small cgroup memory limit, then restoring the limit afterwards.\n"
        "\n"
        "Options:\n"
        "  -m, --limit-mb MB       Memory limit during swapout (default: 8 MB)\n"
        "  -r, --target-rss-kb KB  Target RSS to reach before stopping (default: 16384 kB)\n"
        "  -i, --interval SECS     Poll interval in seconds (default: 1.0)\n"
        "  -n, --max-iter N        Maximum iterations before giving up (default: 60)\n"
        "  -q, --quiet             Less verbose output\n"
        "  -h, --help              Show this help\n"
        "\n"
        "Example:\n"
        "  %s 12345 -m 8 -r 16384 -i 1 -n 60\n",
        prog, prog
    );
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* Read whole file into buffer (small text files only) */
static char *read_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)len, fp);
    fclose(fp);
    buf[n] = '\0';
    return buf;
}

/* Write a string to a file (overwrite) */
static int write_file(const char *path, const char *val) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    size_t len = strlen(val);
    size_t n = fwrite(val, 1, len, fp);
    fclose(fp);
    return (n == len) ? 0 : -1;
}

/* Trim trailing whitespace in-place */
static void rtrim(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (unsigned char)s[len-1] <= 0x20) {
        s[--len] = '\0';
    }
}

/* Sleep in seconds (double) */
static void sleep_double(double secs) {
    if (secs <= 0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)secs;
    ts.tv_nsec = (long)((secs - ts.tv_sec) * 1e9);
    if (ts.tv_nsec < 0) ts.tv_nsec = 0;
    nanosleep(&ts, NULL);
}

/* ---------- proc mem info ---------- */

static int read_proc_meminfo(pid_t pid, proc_meminfo_t *out) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[256];
    long rss_kb = 0;
    long swap_kb = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            /* parse kB */
            char *p = line;
            while (*p && !isdigit((unsigned char)*p)) p++;
            if (*p) rss_kb = strtol(p, NULL, 10);
        } else if (strncmp(line, "VmSwap:", 7) == 0) {
            char *p = line;
            while (*p && !isdigit((unsigned char)*p)) p++;
            if (*p) swap_kb = strtol(p, NULL, 10);
        }
    }
    fclose(fp);

    out->pid = pid;
    out->rss_kb = rss_kb;
    out->swap_kb = swap_kb;
    return 0;
}

/* ---------- cgroup detection & setup ---------- */

static cgroup_version_t detect_cgroup_version(void) {
    /* v2 unified if /sys/fs/cgroup/cgroup.controllers exists */
    if (file_exists("/sys/fs/cgroup/cgroup.controllers")) {
        return CGROUP_V2;
    }
    /* v1 memory controller */
    if (file_exists("/sys/fs/cgroup/memory")) {
        return CGROUP_V1;
    }
    return CGROUP_NONE;
}

/* Creates parent swapout directory if needed */
static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return 0;
        errno = ENOTDIR;
        return -1;
    }
    if (mkdir(path, 0755) != 0) {
        return -1;
    }
    return 0;
}

/* Setup cgroup paths for a given pid */
typedef struct {
    cgroup_version_t ver;
    char group_dir[256];       /* /sys/fs/cgroup/.../swapout/<pid> */
    char procs_path[512];      /* .../cgroup.procs */
    char limit_path[512];      /* memory.high or memory.limit_in_bytes */
    char backup_limit[128];    /* original limit text */
    int  had_backup;
} cgroup_ctx_t;

static int setup_cgroup_for_pid(pid_t pid, cgroup_ctx_t *ctx, int quiet) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->ver = detect_cgroup_version();
    if (ctx->ver == CGROUP_NONE) {
        fprintf(stderr, "No cgroup v1/v2 memory controller detected under /sys/fs/cgroup\n");
        return -1;
    }

    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);

    if (ctx->ver == CGROUP_V2) {
        const char *base = "/sys/fs/cgroup/swapout";
        if (ensure_dir("/sys/fs/cgroup") != 0 && errno != EEXIST) {
            perror("mkdir /sys/fs/cgroup");
            return -1;
        }
        if (ensure_dir(base) != 0 && errno != EEXIST) {
            perror("mkdir /sys/fs/cgroup/swapout");
            return -1;
        }
        snprintf(ctx->group_dir, sizeof(ctx->group_dir), "%s/%s", base, pid_str);
        if (ensure_dir(ctx->group_dir) != 0 && errno != EEXIST) {
            perror("mkdir group_dir");
            return -1;
        }
        snprintf(ctx->procs_path, sizeof(ctx->procs_path), "%s/cgroup.procs", ctx->group_dir);
        snprintf(ctx->limit_path, sizeof(ctx->limit_path), "%s/memory.high", ctx->group_dir);

        if (!quiet)
            printf("[+] cgroup v2 detected, using %s\n", ctx->group_dir);
    } else {
        /* v1 memory controller */
        const char *base = "/sys/fs/cgroup/memory/swapout";
        if (ensure_dir("/sys/fs/cgroup/memory") != 0 && errno != EEXIST) {
            perror("mkdir /sys/fs/cgroup/memory");
            return -1;
        }
        if (ensure_dir(base) != 0 && errno != EEXIST) {
            perror("mkdir /sys/fs/cgroup/memory/swapout");
            return -1;
        }
        snprintf(ctx->group_dir, sizeof(ctx->group_dir), "%s/%s", base, pid_str);
        if (ensure_dir(ctx->group_dir) != 0 && errno != EEXIST) {
            perror("mkdir group_dir");
            return -1;
        }
        snprintf(ctx->procs_path, sizeof(ctx->procs_path), "%s/cgroup.procs", ctx->group_dir);
        snprintf(ctx->limit_path, sizeof(ctx->limit_path), "%s/memory.limit_in_bytes", ctx->group_dir);

        if (!quiet)
            printf("[+] cgroup v1 (memory) detected, using %s\n", ctx->group_dir);
    }

    /* Backup original limit, if readable */
    char *orig = read_file(ctx->limit_path);
    if (orig) {
        rtrim(orig);
        strncpy(ctx->backup_limit, orig, sizeof(ctx->backup_limit) - 1);
        ctx->backup_limit[sizeof(ctx->backup_limit) - 1] = '\0';
        free(orig);
        ctx->had_backup = 1;
        if (!quiet)
            printf("[+] Original limit at %s: '%s'\n", ctx->limit_path, ctx->backup_limit);
    } else {
        ctx->had_backup = 0;
        if (!quiet)
            printf("[!] Could not read original limit at %s, will not restore.\n", ctx->limit_path);
    }

    /* Move PID into this cgroup */
    char pidbuf[32];
    snprintf(pidbuf, sizeof(pidbuf), "%d\n", pid);
    if (write_file(ctx->procs_path, pidbuf) != 0) {
        fprintf(stderr, "Failed to move pid %d into %s: %s\n",
                pid, ctx->procs_path, strerror(errno));
        return -1;
    }
    if (!quiet)
        printf("[+] Moved PID %d into %s\n", pid, ctx->group_dir);

    return 0;
}

/* Apply low memory limit */
static int apply_low_limit(const cgroup_ctx_t *ctx, long limit_mb, int quiet) {
    char buf[64];
    if (ctx->ver == CGROUP_V2) {
        /* memory.high uses "max" or byte value */
        long long bytes = (long long)limit_mb * 1024LL * 1024LL;
        snprintf(buf, sizeof(buf), "%lld\n", bytes);
    } else {
        long long bytes = (long long)limit_mb * 1024LL * 1024LL;
        snprintf(buf, sizeof(buf), "%lld\n", bytes);
    }

    if (!quiet)
        printf("[+] Applying temporary limit %s to %s\n", buf, ctx->limit_path);

    if (write_file(ctx->limit_path, buf) != 0) {
        fprintf(stderr, "Failed to set limit at %s: %s\n", ctx->limit_path, strerror(errno));
        return -1;
    }
    return 0;
}

/* Restore original limit or set to max */
static void restore_limit(const cgroup_ctx_t *ctx, int quiet) {
    if (ctx->ver == CGROUP_NONE) return;

    const char *val = NULL;
    char fallback[16];

    if (ctx->had_backup && ctx->backup_limit[0] != '\0') {
        val = ctx->backup_limit;
    } else {
        if (ctx->ver == CGROUP_V2) {
            strcpy(fallback, "max");
        } else {
            /* v1: a very large number, e.g., "9223372036854771712" (LLONG_MAX-ish) */
            strcpy(fallback, "9223372036854771712");
        }
        val = fallback;
    }

    if (!quiet)
        printf("[+] Restoring limit at %s to '%s'\n", ctx->limit_path, val);

    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s\n", val);
    if (write_file(ctx->limit_path, tmp) != 0) {
        fprintf(stderr, "[!] Failed to restore limit at %s: %s\n",
                ctx->limit_path, strerror(errno));
    }
}

/* Cleanup group dir (best effort) */
static void cleanup_cgroup(const cgroup_ctx_t *ctx, int quiet) {
    if (ctx->group_dir[0] == '\0')
        return;
    if (rmdir(ctx->group_dir) != 0) {
        if (!quiet)
            fprintf(stderr, "[!] Could not remove %s: %s\n",
                    ctx->group_dir, strerror(errno));
    } else {
        if (!quiet)
            printf("[+] Removed cgroup %s\n", ctx->group_dir);
    }
}

/* ---------- main ---------- */

int main(int argc, char **argv) {
    long limit_mb = 8;          /* memory limit during swapout */
    long target_rss_kb = 16384; /* stop when RSS <= this (default 16MB) */
    double interval = 1.0;      /* seconds between polls */
    int max_iter = 60;          /* maximum iterations */
    int quiet = 0;

    static struct option long_opts[] = {
        {"limit-mb",       required_argument, 0, 'm'},
        {"target-rss-kb",  required_argument, 0, 'r'},
        {"interval",       required_argument, 0, 'i'},
        {"max-iter",       required_argument, 0, 'n'},
        {"quiet",          no_argument,       0, 'q'},
        {"help",           no_argument,       0, 'h'},
        {0,0,0,0}
    };

    int opt;
    int opt_index = 0;
    while ((opt = getopt_long(argc, argv, "m:r:i:n:qh", long_opts, &opt_index)) != -1) {
        switch (opt) {
        case 'm':
            limit_mb = strtol(optarg, NULL, 10);
            if (limit_mb <= 0) limit_mb = 8;
            break;
        case 'r':
            target_rss_kb = strtol(optarg, NULL, 10);
            if (target_rss_kb <= 0) target_rss_kb = 16384;
            break;
        case 'i':
            interval = atof(optarg);
            if (interval <= 0) interval = 1.0;
            break;
        case 'n':
            max_iter = atoi(optarg);
            if (max_iter <= 0) max_iter = 60;
            break;
        case 'q':
            quiet = 1;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: PID is required.\n\n");
        usage(argv[0]);
        return 1;
    }

    pid_t pid = (pid_t)atoi(argv[optind]);
    if (pid <= 0) {
        fprintf(stderr, "Invalid PID: %s\n", argv[optind]);
        return 1;
    }

    /* Check that /proc/PID exists */
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);
    if (!file_exists(proc_path)) {
        fprintf(stderr, "No such process: %d\n", pid);
        return 1;
    }

    if (!quiet) {
        printf("[+] swapout: targeting PID %d\n", pid);
        printf("[+] limit_mb=%ld, target_rss_kb=%ld, interval=%.2f, max_iter=%d\n",
               limit_mb, target_rss_kb, interval, max_iter);
    }

    cgroup_ctx_t ctx;
    if (setup_cgroup_for_pid(pid, &ctx, quiet) != 0) {
        fprintf(stderr, "Failed to set up cgroup for pid %d\n", pid);
        return 1;
    }

    if (apply_low_limit(&ctx, limit_mb, quiet) != 0) {
        restore_limit(&ctx, quiet);
        cleanup_cgroup(&ctx, quiet);
        return 1;
    }

    /* Poll until RSS <= target_rss_kb or max_iter reached or process exits */
    int iter = 0;
    int done = 0;

    if (!quiet)
        printf("[+] Forcing swap... polling process memory usage\n");

    while (iter < max_iter) {
        proc_meminfo_t mi;
        if (read_proc_meminfo(pid, &mi) != 0) {
            if (!quiet)
                printf("[!] Process %d no longer exists, stopping.\n", pid);
            done = 1;
            break;
        }

        if (!quiet) {
            printf("  iter %2d: RSS=%ld kB, SWAP=%ld kB\n",
                   iter + 1, mi.rss_kb, mi.swap_kb);
        }

        if (mi.rss_kb <= target_rss_kb) {
            if (!quiet)
                printf("[+] Target RSS reached (<= %ld kB), stopping.\n", target_rss_kb);
            done = 1;
            break;
        }

        iter++;
        sleep_double(interval);
    }

    if (!done && !quiet) {
        printf("[!] max_iter reached without hitting target RSS; restoring anyway.\n");
    }

    restore_limit(&ctx, quiet);
    cleanup_cgroup(&ctx, quiet);

    if (!quiet)
        printf("[+] swapout complete.\n");

    return 0;
}

