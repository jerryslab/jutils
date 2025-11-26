/*
 * swapmon.c
 *
 * Show processes that have pages in swap using /proc/<pid>/status.
 *
 * Modes:
 *   Default: table view (PID, SWAP, CMD)
 *   -f, --full : extended table (PID, SWAP, RSS, VSZ, CMD)
 *   -j, --json : JSON snapshot
 *   -t, --top  : periodically refreshing "top-like" view
 *
 * Build:
 *   gcc -O2 -Wall -Wextra -std=c11 -o swapmon swapmon.c
 *
 * Author: Jerry Richardson <jerry@jerryslab.com>
 * Copyright (C) 2025
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <getopt.h>
#include <time.h>

/* ------------ Data structures ------------ */

typedef struct {
    pid_t pid;
    long swap_kb;
    long rss_kb;
    long vsz_kb;
    char name[64];
    char *cmdline; /* dynamically allocated */
} proc_info;

/* ------------ Utility helpers ------------ */

static int is_number_str(const char *s) {
    if (!s || !*s)
        return 0;
    while (*s) {
        if (!isdigit((unsigned char)*s))
            return 0;
        s++;
    }
    return 1;
}

/* Trim trailing newline and spaces */
static void rtrim(char *s) {
    size_t len;
    if (!s) return;
    len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == ' ' || s[len - 1] == '\t'))
        s[--len] = '\0';
}

/* Read a single long value (kB) from a line like "VmSwap:     128 kB" */
static long parse_kb_value(const char *line) {
    const char *p = line;
    while (*p && !isdigit((unsigned char)*p))
        p++;
    if (!*p)
        return 0;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p)
        return 0;
    return v;
}

/* Read command line from /proc/<pid>/cmdline */
static char *read_cmdline(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    char *buf = NULL;
    size_t sz = 0;
    {
        char tmp[4096];
        size_t n;
        while ((n = fread(tmp, 1, sizeof(tmp), fp)) > 0) {
            char *nb = realloc(buf, sz + n + 1);
            if (!nb) {
                free(buf);
                fclose(fp);
                return NULL;
            }
            buf = nb;
            memcpy(buf + sz, tmp, n);
            sz += n;
        }
    }
    fclose(fp);

    if (!buf || sz == 0) {
        free(buf);
        return NULL;
    }

    /* cmdline is NUL-separated; convert to space-separated string */
    for (size_t i = 0; i < sz; i++) {
        if (buf[i] == '\0')
            buf[i] = ' ';
    }
    buf[sz] = '\0';
    rtrim(buf);
    return buf;
}

/* Read total system swap info from /proc/meminfo */
static void read_system_swap(long *swap_total_kb, long *swap_free_kb) {
    if (swap_total_kb) *swap_total_kb = 0;
    if (swap_free_kb) *swap_free_kb = 0;

    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp)
        return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "SwapTotal:", 10) == 0) {
            if (swap_total_kb)
                *swap_total_kb = parse_kb_value(line);
        } else if (strncmp(line, "SwapFree:", 9) == 0) {
            if (swap_free_kb)
                *swap_free_kb = parse_kb_value(line);
        }
    }
    fclose(fp);
}

/* ------------ Process scanning ------------ */

static proc_info *scan_processes(size_t *out_count) {
    DIR *dp = opendir("/proc");
    if (!dp) {
        fprintf(stderr, "Failed to open /proc: %s\n", strerror(errno));
        return NULL;
    }

    proc_info *list = NULL;
    size_t count = 0;
    struct dirent *de;

    while ((de = readdir(dp)) != NULL) {
        if (!is_number_str(de->d_name))
            continue;

        pid_t pid = (pid_t)atoi(de->d_name);
        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/status", pid);
        FILE *fp = fopen(path, "r");
        if (!fp)
            continue;

        char line[256];
        char name[64] = "";
        long swap_kb = 0;
        long rss_kb = 0;
        long vsz_kb = 0;

        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "Name:", 5) == 0) {
                char *p = line + 5;
                while (*p == ' ' || *p == '\t') p++;
                strncpy(name, p, sizeof(name) - 1);
                name[sizeof(name) - 1] = '\0';
                rtrim(name);
            } else if (strncmp(line, "VmSwap:", 7) == 0) {
                swap_kb = parse_kb_value(line);
            } else if (strncmp(line, "VmRSS:", 6) == 0) {
                rss_kb = parse_kb_value(line);
            } else if (strncmp(line, "VmSize:", 7) == 0) {
                vsz_kb = parse_kb_value(line);
            }
        }
        fclose(fp);

        if (swap_kb <= 0)
            continue; /* Only care about processes with swap usage */

        proc_info pi;
        memset(&pi, 0, sizeof(pi));
        pi.pid = pid;
        pi.swap_kb = swap_kb;
        pi.rss_kb = rss_kb;
        pi.vsz_kb = vsz_kb;
        strncpy(pi.name, name, sizeof(pi.name) - 1);
        pi.name[sizeof(pi.name) - 1] = '\0';

        pi.cmdline = read_cmdline(pid);
        if (!pi.cmdline || pi.cmdline[0] == '\0') {
            /* Fallback: just use name if cmdline is unavailable */
            free(pi.cmdline);
            pi.cmdline = strdup(pi.name);
        }

        proc_info *newlist = realloc(list, (count + 1) * sizeof(proc_info));
        if (!newlist) {
            free(pi.cmdline);
            break;
        }
        list = newlist;
        list[count++] = pi;
    }

    closedir(dp);
    if (out_count)
        *out_count = count;
    return list;
}

/* Sort by swap descending */
static int cmp_swap_desc(const void *a, const void *b) {
    const proc_info *pa = a;
    const proc_info *pb = b;
    if (pa->swap_kb < pb->swap_kb) return 1;
    if (pa->swap_kb > pb->swap_kb) return -1;
    return (pa->pid > pb->pid) - (pa->pid < pb->pid);
}

/* Free list */
static void free_proc_list(proc_info *list, size_t count) {
    if (!list) return;
    for (size_t i = 0; i < count; i++) {
        free(list[i].cmdline);
    }
    free(list);
}

/* ------------ Output modes ------------ */

static void print_table_simple(proc_info *list, size_t count) {
    printf("%-7s %-10s %s\n", "PID", "SWAP(kB)", "CMD");
    for (size_t i = 0; i < count; i++) {
        printf("%-7d %-10ld %s\n",
               list[i].pid,
               list[i].swap_kb,
               list[i].cmdline ? list[i].cmdline : list[i].name);
    }
}

static void print_table_full(proc_info *list, size_t count) {
    printf("%-7s %-10s %-10s %-10s %s\n",
           "PID", "SWAP(kB)", "RSS(kB)", "VSZ(kB)", "CMD");
    for (size_t i = 0; i < count; i++) {
        printf("%-7d %-10ld %-10ld %-10ld %s\n",
               list[i].pid,
               list[i].swap_kb,
               list[i].rss_kb,
               list[i].vsz_kb,
               list[i].cmdline ? list[i].cmdline : list[i].name);
    }
}

/* Minimal JSON escaping: escape backslash and quote */
static void json_escape(const char *s, FILE *out) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\\' || c == '"') {
            fputc('\\', out);
            fputc(c, out);
        } else if (c == '\n') {
            fputs("\\n", out);
        } else if (c == '\r') {
            fputs("\\r", out);
        } else if (c == '\t') {
            fputs("\\t", out);
        } else if (c < 32) {
            /* control chars as \u00XX */
            fprintf(out, "\\u%04x", c);
        } else {
            fputc(c, out);
        }
    }
}

static void print_json(proc_info *list, size_t count) {
    long swap_total = 0, swap_free = 0;
    read_system_swap(&swap_total, &swap_free);

    printf("{\n");
    printf("  \"swap_total_kb\": %ld,\n", swap_total);
    printf("  \"swap_free_kb\": %ld,\n", swap_free);
    printf("  \"processes\": [\n");

    for (size_t i = 0; i < count; i++) {
        const proc_info *p = &list[i];
        printf("    {\n");
        printf("      \"pid\": %d,\n", p->pid);
        printf("      \"name\": \"");
        json_escape(p->name, stdout);
        printf("\",\n");
        printf("      \"swap_kb\": %ld,\n", p->swap_kb);
        printf("      \"rss_kb\": %ld,\n", p->rss_kb);
        printf("      \"vsz_kb\": %ld,\n", p->vsz_kb);
        printf("      \"cmd\": \"");
        json_escape(p->cmdline ? p->cmdline : p->name, stdout);
        printf("\"\n");
        printf("    }%s\n", (i + 1 < count) ? "," : "");
    }

    printf("  ]\n");
    printf("}\n");
}

/* Simple top-like mode */
static void run_top_mode(int full, double delay_sec, int max_iters) {
    int iter = 0;
    for (;;) {
        size_t count = 0;
        proc_info *list = scan_processes(&count);
        if (!list) {
            fprintf(stderr, "Failed to scan processes\n");
            return;
        }
        qsort(list, count, sizeof(proc_info), cmp_swap_desc);

        long swap_total = 0, swap_free = 0;
        read_system_swap(&swap_total, &swap_free);
        long swap_used = swap_total - swap_free;

        /* Clear screen, move cursor home */
        printf("\033[H\033[J");

        time_t now = time(NULL);
        char buf[64];
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);

        printf("swapmon - processes with swapped pages   %s\n", buf);
        printf("System swap: used %ld kB / total %ld kB\n\n",
               swap_used, swap_total);

        if (full)
            print_table_full(list, count);
        else
            print_table_simple(list, count);

        fflush(stdout);
        free_proc_list(list, count);

        iter++;
        if (max_iters > 0 && iter >= max_iters)
            break;

        struct timespec ts;
        ts.tv_sec = (time_t)delay_sec;
        ts.tv_nsec = (long)((delay_sec - ts.tv_sec) * 1e9);
        if (ts.tv_nsec < 0) ts.tv_nsec = 0;
        nanosleep(&ts, NULL);
    }
}

/* ------------ CLI / main ------------ */

static void print_help(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "List processes that have pages in swap (VmSwap > 0).\n"
        "\n"
        "Modes (choose one):\n"
        "  (default)   Table: PID, SWAP(kB), CMD\n"
        "  -f, --full  Extended table: PID, SWAP, RSS, VSZ, CMD\n"
        "  -j, --json  JSON output snapshot\n"
        "  -t, --top   Continuously refreshing top-like view\n"
        "\n"
        "Top mode options:\n"
        "  -d, --delay SECS   Refresh interval (default: 2.0)\n"
        "  -n, --count N      Number of iterations (default: infinite)\n"
        "\n"
        "Other:\n"
        "  -h, --help         Show this help\n"
        "\n"
        "Examples:\n"
        "  %s            # simple table\n"
        "  %s -f         # full table with RSS/VSZ\n"
        "  %s -j         # JSON snapshot\n"
        "  %s -t -d 1.0  # top-mode, 1 second refresh\n",
        prog, prog, prog, prog, prog
    );
}

int main(int argc, char **argv) {
    int opt;
    int mode_full = 0;
    int mode_json = 0;
    int mode_top = 0;
    double delay_sec = 2.0;
    int max_iters = 0; /* 0 = infinite */

    static struct option long_opts[] = {
        {"full",  no_argument,       0, 'f'},
        {"json",  no_argument,       0, 'j'},
        {"top",   no_argument,       0, 't'},
        {"delay", required_argument, 0, 'd'},
        {"count", required_argument, 0, 'n'},
        {"help",  no_argument,       0, 'h'},
        {0,0,0,0}
    };

    while ((opt = getopt_long(argc, argv, "fjtd:n:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'f':
            mode_full = 1;
            break;
        case 'j':
            mode_json = 1;
            break;
        case 't':
            mode_top = 1;
            break;
        case 'd':
            delay_sec = atof(optarg);
            if (delay_sec <= 0.0) delay_sec = 1.0;
            break;
        case 'n':
            max_iters = atoi(optarg);
            if (max_iters < 0) max_iters = 0;
            break;
        case 'h':
            print_help(argv[0]);
            return 0;
        default:
            print_help(argv[0]);
            return 1;
        }
    }

    /* Mutually exclusive modes: json vs top; default is table */
    if (mode_json && mode_top) {
        fprintf(stderr, "Cannot use --json and --top together.\n");
        return 1;
    }

    if (mode_top) {
        run_top_mode(mode_full, delay_sec, max_iters);
        return 0;
    }

    /* Snapshot modes (table/json) */

    size_t count = 0;
    proc_info *list = scan_processes(&count);
    if (!list) {
        fprintf(stderr, "Failed to scan processes\n");
        return 1;
    }

    qsort(list, count, sizeof(proc_info), cmp_swap_desc);

    if (mode_json) {
        print_json(list, count);
    } else if (mode_full) {
        print_table_full(list, count);
    } else {
        print_table_simple(list, count);
    }

    free_proc_list(list, count);
    return 0;
}

