#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

size_t compress_frame(const char *frame, size_t len, uint8_t *out)
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
            return 0;   // caractère invalide

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

size_t decompress_frame(const uint8_t *in, size_t in_len,
                        char *out, size_t out_len)
{
    size_t i = 0;
    size_t o = 0;

    while (i < in_len && o < out_len)
    {
        uint8_t b = in[i++];

        uint8_t run = b >> 3;
        uint8_t idx = b & 0x07;

        if (run == 0 || idx >= ascii_shades_len)
            break;   // flux invalide

        char c = ascii_shades[idx];

        while (run-- && o < out_len)
            out[o++] = c;
    }

    return o;
}

void print_decompressed(FILE *in, size_t w, size_t h)
{
    uint8_t b;
    size_t x=0,y=0;

    while ((b=fgetc(in))!=EOF)
    {
        uint8_t run = b >> 3;
        uint8_t idx = b & 0x07;

        if (run == 0 || idx >= ascii_shades_len)
            break;   // flux invalide

        char c = ascii_shades[idx];

        while (run--)
        {
            // print the char and check for screen boundaries
            putc(c, stdout);

            x++;
            if (x==w)
            {
                putc('\n', stdout);
                x=0;
                y++;
            }
            if (y==h)
            {
                // goto to top left of screen
                putc('\033', stdout);
                putc('[', stdout);
                putc('H', stdout);
                x=0;
                y=0;
                usleep(33000); // hard coded 30 FPS
            }
        }
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
            fprintf(stderr,"Usage: encode WIDTH HEIGHT input.txt output.bin\n");
            return EXIT_FAILURE;
        }
        char frame[w*h];
        size_t idx = 0;
    
        /* Read characters, ignore '\n' */
        while (idx < (size_t)(w * h))
        {
            int c = fgetc(f);
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
    
            frame[idx++] = (char)c;
        }
    
        fclose(f);
    
        uint8_t compressed[4096]; // hope this is sufficient :)
        size_t out_len = compress_frame(frame, w*h, compressed);
    
        //printf("Frame size      : %d bytes\n", w * h);
        //printf("Compressed size : %zu bytes\n", out_len);
        //printf("Ratio           : %.2f %%\n", 100.0 * (double)out_len / (double)(w * h));
    
        //for (size_t i = 0; i < out_len; i++)
        //    printf("%02X ", compressed[i]);
        //printf("\n");
    
        char decoded[w*h];
        size_t dec_len = decompress_frame(compressed, out_len, decoded, w*h);
        
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
    
        char* outfile = argv[4];
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
        print_decompressed(f, w, h);
        fclose(f);

    }
    else
    {
        fprintf(stderr, "Binary name should be in {./encode,./decode}!\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
