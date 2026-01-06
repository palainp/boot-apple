#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// #define DEBUG

static const char ascii_shades[] = " .:+=#%@";
static size_t ascii_shades_len = 8;

static size_t sym_index(char c)
{
    size_t i=0;
    while (i<ascii_shades_len && c!=ascii_shades[i])
        i++;
    return i; // =len_charcodes stands for not a charcode
}

static bool is_valid_char(char c)
{
    return (sym_index(c) != ascii_shades_len);
}

/* -----------------------
 * Encoder
 */

size_t encode_frame_RLE(const char *frame, size_t len, uint8_t *out)
{
    size_t i = 0;
    size_t o = 0;

    while (i < len) {
        char c = frame[i];
        size_t run = 1;

        while (i + run < len && frame[i + run] == c && run < 127)
            run++;

        int idx = sym_index(c);
        if (idx >= ascii_shades_len)
            return 0;

        size_t r = run;
        while (r > 0) {
            uint8_t chunk = (r > 31) ? 31 : (uint8_t)r;
            out[o++] = (chunk << 3) | (uint8_t)idx;
            r -= chunk;
        }

        i += run;
    }

    return o;
}

// encode delta from prev, skip '\n'
size_t encode_frame_delta(const char *prev, const char *cur, size_t len, uint8_t *out)
{
    size_t i = 0, o = 0;
    while (i < len) {
        /* SKIP unchanged */
        if (prev[i] == cur[i]) {
            size_t run = 1;
            while (i + run < len &&
                   prev[i + run] == cur[i + run] &&
                   run < 63)
                run++;

            out[o++] = (0 << 6) | run;
            i += run;
        }
        /* RLE for ' ' and '@' */
        else if (cur[i] == ' ' || cur[i] == '@') {
            char c = cur[i];
            size_t run = 1;
            while (i + run < len &&
                   cur[i + run] == c &&
                   prev[i + run] != cur[i + run] &&
                   run < 63)
                run++;

            uint8_t opcode = (c == ' ') ? 2 : 3;
            out[o++] = (opcode << 6) | run;
            i += run;
        }
        /* COPY literal */
        else {
            size_t run = 1;
            while (i + run < len &&
                   prev[i + run] != cur[i + run] &&
                   cur[i + run] != ' ' &&
                   cur[i + run] != '@' &&
                   run < 63)
                run++;

            out[o++] = (1 << 6) | run;
            for (size_t k = 0; k < run; k++)
                out[o++] = cur[i + k];

            i += run;
        }
    }

    return o;
}

size_t encode_frame(const char *prev, const char *cur, size_t len, uint8_t *out)
{
    uint8_t buf1[2048]; // hope this is sufficient :)
    uint8_t buf2[2048];

    size_t changes = 0;

    /* First pass: detect if identical */
    for (size_t k = 0; k < len; k++) {
        if (prev[k] != cur[k]) {
            changes++;
            break;
        }
    }

    if (changes == 0) {
        out[0] = 0x00; // FRAME_IDENTICAL
        return 1;
    }

    size_t out_rle  = encode_frame_RLE(cur, len, buf1);
    size_t out_delta = encode_frame_delta(prev, cur, len, buf2);

    if (out_delta < out_rle && out_delta < len) {
        out[0] = 0x02;
        memcpy(out+1, buf2, out_delta);
        return out_delta + 1;
    } else {
        out[0] = 0x01;
        memcpy(out+1, buf1, out_rle);
        return out_rle + 1;
    }
}

/* -----------------------
 * Decoder
 */

size_t decode_frame_RLE(const uint8_t *in, size_t in_len, size_t *consumed, char *out, size_t out_len)
{
    size_t i = 0;
    size_t o = 0;

    while (i < in_len && o < out_len)
    {
        uint8_t b = in[i++];

        uint8_t run = b >> 3;
        uint8_t idx = b & 0x07;

        if (run == 0 || idx >= ascii_shades_len)
            break;

        char c = ascii_shades[idx];

        while (run-- && o < out_len)
            out[o++] = c;
    }
    *consumed = i;

    return o;
}

size_t decode_frame_delta(const uint8_t *in, size_t in_len, size_t *consumed, const char *prev, char *out, size_t out_len)
{
    size_t i = 0, o = 0;

    if (in_len == 1 && in[0] == 0x00) {
        memcpy(out, prev, out_len);
        return out_len;
    }

    while (i < in_len && o < out_len) {
        uint8_t b = in[i++];
        uint8_t opcode = b >> 6;
        uint8_t run = b & 0x3F;

        switch (opcode) {
        case 0: // SKIP
            memcpy(out + o, prev + o, run);
            o += run;
            break;

        case 1: // COPY
            for (uint8_t k = 0; k < run; k++)
                out[o++] = in[i++];
            break;

        case 2: // RLE_SPACE
            memset(out + o, ' ', run);
            o += run;
            break;

        case 3: // RLE_AT
            memset(out + o, '@', run);
            o += run;
            break;
        }
    }

    *consumed = i;
    return o;
}

size_t decode_frame(const uint8_t *in, size_t in_len, size_t *consumed, const char *prev, char *out, size_t out_len)
{
    switch (in[0])
    {
        case 0x00: // identical frame
            *consumed = 1;
            memcpy(out, prev, out_len);
            return out_len;
        case 0x01: // use RLE
            out_len = decode_frame_RLE(in+1, in_len-1, consumed, out, out_len);
            *consumed += 1;
            return out_len;
        case 0x02: // use delta
            out_len = decode_frame_delta(in+1, in_len-1, consumed, prev, out, out_len);
            *consumed += 1;
            return out_len;
        default:
            printf("bad frame first byte\n");
            exit(EXIT_FAILURE);
    }
}

/* -----------------------
 * Live decoder
 */

void print_decompressed(uint8_t *stream, size_t in_len, size_t w, size_t h)
{
    char buf[2048];
    size_t x=0,y=0;
    size_t screen_len = w*h;

    // first wipe screen, first frame is considered black
    memset(buf, ' ', screen_len);

    size_t i=0;
    size_t frame=0;
    while(i<in_len)
    {
        char prev[2048];
        memcpy(prev, buf, screen_len);

        size_t consumed = 0;
        decode_frame(&(stream[i]), in_len-i, &consumed, prev, buf, screen_len);
        i += consumed;

        // goto to top left of screen
        putc('\033', stdout);
        putc('[', stdout);
        putc('H', stdout);
        printf("frame %d, offset %d \n", frame, i);
        for(y=0; y<h; ++y)
        {
            for (x=0; x<w; ++x)
            {
                putc(buf[x+y*w], stdout);
            }
            putc('\n', stdout);
        }
        frame++;
        usleep(33000); // hard coded 30 FPS
    }

    putc('\n', stdout);
}

int main(int argc, char* argv[])
{
    if(argc < 3)
    {
        fprintf(stderr,"Usage: {encode,decode} WIDTH HEIGHT input.txt [output.bin]\n");
        return EXIT_FAILURE;
    }

    int w = atoi(argv[1]);
    int h = atoi(argv[2]);
    if (w <= 0 || h <= 0)
    {
        fprintf(stderr, "Invalid dimensions\n");
        return EXIT_FAILURE;
    }

    char* infile = argv[3];
    FILE* f = fopen(infile, "r");
    if (!f)
    {
        perror("fopen");
        return EXIT_FAILURE;
    }
    
    if (strcmp(argv[0], "./encode") == 0)
    {
        if(argc < 5)
        {
            fprintf(stderr,"Usage: encode WIDTH HEIGHT input.txt previous.bin output.bin\n");
            return EXIT_FAILURE;
        }

        char frame[w*h];
        size_t idx = 0;
    
        /* Read characters, ignore '\n' */
        int c;
        while ((c = fgetc(f))!=EOF)
        {
            if (c == EOF)
            {
                fprintf(stderr, "Input file too short\n");
                fclose(f);
                return EXIT_FAILURE;
            }

            if (c == '\n')
                continue;
    
            if (!is_valid_char((char)c))
            {
                fprintf(stderr, "Invalid character '%c' in input\n", c);
                fclose(f);
                return EXIT_FAILURE;
            }
    
            frame[idx++] = c;
        }
        fclose(f);


        char* prevfile = argv[4];
        FILE* p = fopen(prevfile, "r");
        if (!p)
        {
            perror("fopen");
            return EXIT_FAILURE;
        }

        idx = 0;
        char prev[2048]; // that should be enough, prev is the compressed form of the previous frame
        /* Read characters, ignore '\n' */
        while ((c = fgetc(p))!=EOF)
        {
            if (c == EOF)
            {
                fprintf(stderr, "Previous frame file too short\n");
                fclose(p);
                return EXIT_FAILURE;
            }

            if (c == '\n')
                continue;
    
            if (!is_valid_char((char)c))
            {
                fprintf(stderr, "Invalid character '%c' in previous frame\n", c);
                fclose(p);
                return EXIT_FAILURE;
            }
    
            prev[idx++] = c;
        }
        fclose(p);

        uint8_t compressed[2048]; // hope this is sufficient :)

        // size_t out_len = compress_frame(frame, w*h, compressed);
        // size_t out_len = encode_frame_delta(prev, frame, w*h, compressed);
        size_t out_len = encode_frame(prev, frame, w*h, compressed);
    
#ifdef DEBUG
        fprintf(stderr, "Frame name      : %s\n", infile);
        fprintf(stderr, "Frame size      : %d bytes\n", w * h);
        fprintf(stderr, "Compressed size : %zu bytes\n", out_len);
        fprintf(stderr, "Compressed type : %s\n", (compressed[0]==0x00?"identical":(compressed[0]==0x01?"RLE":"delta")));
        fprintf(stderr, "Ratio           : %.2f %%\n", 100.0 * (double)out_len / (double)(w * h));
#endif

        char decoded[w*h];
        size_t consumed = 0;
        // size_t dec_len = decompress_frame(compressed, out_len, decoded, w*h);
        // size_t dec_len = decode_frame_delta(compressed, out_len, prev, decoded, w*h);
        size_t dec_len = decode_frame(compressed, out_len, &consumed, prev, decoded, w*h);
        
        if (dec_len != (size_t)(w*h))
        {
            fprintf(stderr, "Decompression failed (%zu/%d)\n", dec_len, w*h);
            return EXIT_FAILURE;
        }
        
        if (memcmp(frame, decoded, w*h) != 0)
        {
            fprintf(stderr, "Mismatch after decompression!\n");
            return EXIT_FAILURE;
        }
    
        char* outfile = argv[5];
        f = fopen(outfile, "w");
        if (!f)
        {
            perror("fopen");
            return EXIT_FAILURE;
        }
        idx = 0;
        while (idx < out_len)
        {
            fputc(compressed[idx], f);
            idx++;
        }
        fclose(f);
    }
    else if (strcmp(argv[0], "./decode") == 0)
    {
        uint8_t stream[2*1024*1024]; // max 2MB stream file

        int b;
        size_t in_len=0;

        while (in_len<sizeof(stream) && ((b=fgetc(f))!=EOF))
        {
            stream[in_len++] = b;
        }
        fclose(f);

        if (in_len==sizeof(stream))
        {
            printf("encoded file is too large\n");
            exit(EXIT_FAILURE);
        }

        print_decompressed(stream, in_len, w, h);
    }
    else
    {
        fprintf(stderr, "Binary name should be in {./encode,./decode}!\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
