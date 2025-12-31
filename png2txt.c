#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

char char_of_lum(uint8_t r, uint8_t g, uint8_t b)
{
    const char ascii_shades[] = " .:+=#%@";
    double lum = (0.299*r + 0.587*g + 0.114*b) / 255.0;
    int idx = (int)(lum * (sizeof(ascii_shades)-2));
    return ascii_shades[idx];
}

uint8_t* resize_image(uint8_t* img, int img_w, int img_h, int target_w, int target_h)
{
    uint8_t* out_pixels = malloc(target_w * target_h * 4);
    if(!out_pixels)
    {
    	fprintf(stderr,"malloc failed\n");
    	exit(1);
    }

    for(int y=0; y<target_h; y++)
    {
        int sy = (int)((double)y * img_h / target_h);
        for(int x=0; x<target_w; x++)
        {
            int sx = (int)((double)x * img_w / target_w);
            for(int c=0; c<4; c++)
            {
                out_pixels[(y*target_w + x)*4 + c] = img[(sy*img_w + sx)*4 + c];
            }
        }
    }
    return out_pixels;
}

void print_resized_image(uint8_t* pixels, int w, int h)
{
    char* buf = malloc((w+1) * h);
    if(!buf)
    {
    	fprintf(stderr,"malloc failed\n");
    	exit(1);
    }
    for(int y=0; y<h; y++)
    {
        for(int x=0; x<w; x++)
        {
            uint8_t r = pixels[(y*w + x)*4 + 0];
            uint8_t g = pixels[(y*w + x)*4 + 1];
            uint8_t b = pixels[(y*w + x)*4 + 2];
            buf[y*(w+1) + x] = char_of_lum(r,g,b);
        }
        buf[y*(w+1) + w] = '\n';
    }
    fwrite(buf,1,(w+1)*h,stdout);
    free(buf);
}

int main(int argc, char** argv)
{
    if(argc != 4)
    {
        fprintf(stderr,"Usage: %s WIDTH HEIGHT input.png\n", argv[0]);
        return EXIT_FAILURE;
    }

    int w = atoi(argv[1]);
    int h = atoi(argv[2]);
    char* infile = argv[3];

    int img_w, img_h, channels;
    uint8_t* img = stbi_load(infile, &img_w, &img_h, &channels, 4);
    if(!img)
    {
        fprintf(stderr,"Failed to load image: %s\n", infile);
        return EXIT_FAILURE;
    }

    uint8_t* resized = resize_image(img, img_w, img_h, w, h);
    print_resized_image(resized, w, h);

    stbi_image_free(img);
    free(resized);
    return EXIT_SUCCESS;
}
