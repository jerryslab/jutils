/*
 * clipit.c
 * Simple standalone OSC52 clipboard utility
 *
 * Compile:
 *     gcc -O2 -o clipit clipit.c
 *
 * Usage:
 *     clipit < file
 *     clipit file.txt
 *     cat file | clipit
 *
 * Author: Jerry Richardson <jerry@jerryslab.com>
 * Copyright (C) 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----- Adjust maximum size here ----- */
/* Most terminals allow 1–4 MB. Increase if needed. */
#define MAX_INPUT_SIZE (4 * 1024 * 1024)

/* Base64 encoding table */
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* ----------- Usage function ----------- */
void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS] [FILE]\n"
        "\n"
        "Copy text to the terminal clipboard using OSC52.\n"
        "If FILE is omitted, %s reads from stdin.\n"
        "\n"
        "Options:\n"
        "  -n        Do NOT send OSC52 terminator (rarely needed)\n"
        "  -h        Show this help message\n"
        "\n"
        "Examples:\n"
        "  %s bigfile.txt\n"
        "  cat file.txt | %s\n"
        "  dmesg | %s -n\n",
        prog, prog, prog, prog, prog
    );
}

/* Encode raw bytes to Base64 into a malloc() buffer */
char *base64_encode(const unsigned char *data, size_t len, size_t *out_len) {
    /* 4 bytes output for every 3 bytes input */
    size_t olen = 4 * ((len + 2) / 3);
    char *out = malloc(olen + 1);
    if (!out) return NULL;

    const unsigned char *end = data + len;
    const unsigned char *in = data;
    char *pos = out;

    while (end - in >= 3) {
        *pos++ = b64_table[in[0] >> 2];
        *pos++ = b64_table[((in[0] & 3) << 4) | (in[1] >> 4)];
        *pos++ = b64_table[((in[1] & 0xf) << 2) | (in[2] >> 6)];
        *pos++ = b64_table[in[2] & 0x3f];
        in += 3;
    }

    /* Pad remaining bytes */
    if (end - in) {
        *pos++ = b64_table[in[0] >> 2];
        if (end - in == 1) {
            *pos++ = b64_table[(in[0] & 3) << 4];
            *pos++ = '=';
        } else {
            *pos++ = b64_table[((in[0] & 3) << 4) | (in[1] >> 4)];
            *pos++ = b64_table[(in[1] & 0xf) << 2];
        }
        *pos++ = '=';
    }

    *pos = '\0';
    if (out_len) *out_len = pos - out;
    return out;
}

int main(int argc, char **argv) {
    int no_term = 0;
    const char *file = NULL;

    /* --- Parse options --- */
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-h")) {
                print_usage(argv[0]);
                return 0;
            }
            else if (!strcmp(argv[i], "-n")) {
                no_term = 1;
            }
            else if (argv[i][0] != '-') {
                file = argv[i];
            }
            else {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        }
    } else {
        /* No args: print usage */
        print_usage(argv[0]);
        return 1;
    }

    /* --- Allocate buffer --- */
    unsigned char *buf = malloc(MAX_INPUT_SIZE);
    if (!buf) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    size_t n = 0;

    /* --- Load from file or stdin --- */
    if (file) {
        FILE *fp = fopen(file, "rb");
        if (!fp) {
            fprintf(stderr, "Could not open file: %s\n", file);
            free(buf);
            return 1;
        }
        n = fread(buf, 1, MAX_INPUT_SIZE, fp);
        fclose(fp);
    } else {
        /* Read stdin */
        n = fread(buf, 1, MAX_INPUT_SIZE, stdin);
    }

    if (n == 0 && ferror(stdin)) {
        fprintf(stderr, "Error reading input\n");
        free(buf);
        return 1;
    }

    /* Base64 encode */
    size_t b64len;
    char *b64 = base64_encode(buf, n, &b64len);
    free(buf);

    if (!b64) {
        fprintf(stderr, "Base64 encode failed\n");
        return 1;
    }

    /* Emit OSC52 escape sequence */
    if (no_term)
        printf("\033]52;c;%s", b64);
    else
        printf("\033]52;c;%s\a", b64);

    fflush(stdout);
    free(b64);
    return 0;
}

