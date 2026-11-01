/* Stand-in for wamrc: the WL82 host runs wasm, so AOT output is unused.
 * --version must print exactly "wamrc 2.4.3". Any other invocation copies
 * the last non-option argument to the -o path. */
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *out = NULL;
    const char *src = NULL;
    int i;
    char buf[1 << 16];

    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        printf("wamrc 2.4.3\n");
        return 0;
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out = argv[++i];
            continue;
        }
        if (argv[i][0] != '-') {
            src = argv[i];
        }
    }
    if (out == NULL || src == NULL) {
        fprintf(stderr, "wamrc_skip: need -o <out> <in>\n");
        return 1;
    }
    FILE *in = fopen(src, "rb");
    FILE *dst = fopen(out, "wb");
    if (in == NULL || dst == NULL) {
        fprintf(stderr, "wamrc_skip: open failed\n");
        return 2;
    }
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            fprintf(stderr, "wamrc_skip: write failed\n");
            return 3;
        }
    }
    fclose(in);
    fclose(dst);
    return 0;
}
