/*
 * mkprelude.c - compress a Forth prelude source into a C header.
 *
 * usage: mkprelude <input.jit> <output.h>
 *
 * Produces prelude_z[] (zlib stream), prelude_z_len and prelude_raw_len.
 */
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input> <output.h>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    if (fseek(f, 0, SEEK_END) != 0) { perror("fseek"); fclose(f); return 1; }
    long n = ftell(f);
    if (n < 0) { perror("ftell"); fclose(f); return 1; }
    rewind(f);

    unsigned char *in = malloc((size_t)n ? (size_t)n : 1);
    if (!in) { fputs("out of memory\n", stderr); fclose(f); return 1; }
    if (fread(in, 1, (size_t)n, f) != (size_t)n) { fputs("read error\n", stderr); fclose(f); return 1; }
    fclose(f);

    uLongf clen = compressBound((uLong)n);
    unsigned char *cbuf = malloc(clen);
    if (!cbuf) { fputs("out of memory\n", stderr); free(in); return 1; }
    if (compress2(cbuf, &clen, in, (uLong)n, Z_BEST_COMPRESSION) != Z_OK) {
        fputs("compression failed\n", stderr); free(in); free(cbuf); return 1;
    }

    FILE *o = fopen(argv[2], "wb");
    if (!o) { perror(argv[2]); free(in); free(cbuf); return 1; }

    fprintf(o, "/* generated from %s by mkprelude; do not edit */\n", argv[1]);
    fprintf(o, "#ifndef PRELUDE_BLOB_H\n#define PRELUDE_BLOB_H\n\n");
    fprintf(o, "static const unsigned char prelude_z[] = {");
    for (uLong i = 0; i < clen; i++) {
        if (i % 16 == 0) fprintf(o, "\n\t");
        fprintf(o, "0x%02x,", cbuf[i]);
    }
    fprintf(o, "\n};\n\n");
    fprintf(o, "static const unsigned long prelude_z_len = %luUL;\n", (unsigned long)clen);
    fprintf(o, "static const unsigned long prelude_raw_len = %luUL;\n", (unsigned long)n);
    fprintf(o, "\n#endif /* PRELUDE_BLOB_H */\n");
    fclose(o);

    free(in);
    free(cbuf);
    return 0;
}
