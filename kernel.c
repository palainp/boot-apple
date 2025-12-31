#include <stdint.h>
#include <stddef.h>

// defined in frames.c
extern unsigned char encoded_bin[];
extern unsigned int encoded_bin_len;

//#define DEBUG

#if !(defined(SCREEN_WIDTH) && defined(SCREEN_HEIGHT) && defined(FPS))
    #error "You must use -DSCREEN_WIDTH and -DSCREEN_HEIGHT and -DFPS"
#endif

static const char ascii_shades[] = " .:+=#%@";
static size_t ascii_shades_len = 8;

volatile uint32_t loop_counter;
uint32_t nops_per_frame;

#define VIDEO_MEMORY ((volatile char*) 0xb8000)
#define FRAME_SIZE (SCREEN_WIDTH*SCREEN_HEIGHT)

#define VGA_CTRL_REGISTER 0x3d4
#define VGA_DATA_REGISTER 0x3d5
#define VGA_OFFSET_LOW    0x0f
#define VGA_OFFSET_HIGH   0x0e
volatile uint8_t* vga = VIDEO_MEMORY;

///////////////////////// ASSEMBLY

static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ __volatile__("outb %0,%1" : : "a" (v), "dN" (port));
}

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void wait_cycles(size_t cycles)
{
    uint64_t start = rdtsc();
    while (rdtsc() - start < cycles) {
        __asm__ volatile ("nop");
    }
}

uint64_t calibrate(void)
{
    uint64_t t0 = rdtsc();
    volatile uint32_t i;
    wait_cycles(10000000);
    uint64_t t1 = rdtsc();
    return t1 - t0;
}

///////////////////////// HELPERS
#ifdef DEBUG
void set_cursor(uint32_t offset)
{
    offset /= 2;
    outb(VGA_CTRL_REGISTER, VGA_OFFSET_HIGH);
    outb(VGA_DATA_REGISTER, (unsigned char) (offset >> 8));
    outb(VGA_CTRL_REGISTER, VGA_OFFSET_LOW);
    outb(VGA_DATA_REGISTER, (unsigned char) (offset & 0xff));
}

void print_str(const char* str, const size_t len, size_t pos)
{
    // set_cursor(pos);
    for (size_t i=0; i<len; ++i)
    {
        vga[pos]     = str[i];
        vga[pos + 1] = 0x07;

        pos+=2;
    }
}

void print_nbr(unsigned int n, int row, int col)
{
    int pos = 0;
    unsigned int div = 1000000;
    int started = 0;
    while (div > 0) {
        unsigned int digit = n / div;
        n %= div;
        div /= 10;

        if (digit != 0 || started || div == 0) {
            vga[2*(row*SCREEN_WIDTH + col + pos)]     = '0' + digit;
            vga[2*(row*SCREEN_WIDTH + col + pos) + 1] = 0x07;
            pos++;
            started = 1;
        }
    }
    vga[2*(row*SCREEN_WIDTH + col + pos)]     = ' ';
    vga[2*(row*SCREEN_WIDTH + col + pos) + 1] = 0x07;
}

char nib2char(uint8_t n)
{
	if (n<10)
	{
		return '0'+n;
	} else if (n<16) {
		return 'A'+n-10;
	} else return '@';
}

void print_hex8(uint8_t n, int row, int col)
{
    vga[2*(row*SCREEN_WIDTH + col)]     = nib2char((n >> 4) & 0xF);
    vga[2*(row*SCREEN_WIDTH + col) + 1] = 0x07;

    vga[2*(row*SCREEN_WIDTH + col + 1)]     = nib2char(n & 0xF);
    vga[2*(row*SCREEN_WIDTH + col + 1) + 1] = 0x07;

    vga[2*(row*SCREEN_WIDTH + col + 2)]     = ' ';
    vga[2*(row*SCREEN_WIDTH + col + 2) + 1] = 0x07;
}

void print_hex16(uint16_t n, int row, int col)
{
	print_hex8(n>>8, row, col);
	print_hex8(n&0xff, row, col+2);
}

void print_hex32(uint32_t n, int row, int col)
{
	print_hex16(n>>16, row, col);
	print_hex16(n&0xffff, row, col+4);
}
#endif

///////////////////////// DECOMPRESSION

void print_decompressed(uint8_t *addr)
{
    uint8_t b;
    uint32_t cursor = 0;

    size_t x=0,y=0;
    while (1)
    {
        b=*addr++;
#ifdef DEBUG
        print_nbr(x, 24, 30);
        print_nbr(y, 24, 40);
#endif

        uint8_t run = b >> 3;
        uint8_t idx = b & 0x07;

        if (run == 0 || idx >= ascii_shades_len)
            break;   // whoops

        char c = ascii_shades[idx];

        while (run--)
        {
            // print the char and check for screen boundaries
            vga[cursor]     = c;
            vga[cursor + 1] = 0x07;
            cursor += 2;

            x++;
            if (x==SCREEN_WIDTH)
            {
                x=0;
                y++;
                cursor = y * 2 * SCREEN_WIDTH;
            }
            if (y==SCREEN_HEIGHT)
            {
                // this frame is printed, sleep XXms
                wait_cycles(nops_per_frame);

                // goto to top left of screen next frame
                x=0;
                y=0;
                cursor = 0;
            }
        }
    }
}

///////////////////////// START ENTRY

int main()
{
    // calibrate 8 times and take average
    // FIXME: get better calibration
    #define N_CAL 8
    size_t loops_cycles = 0;
    for (size_t i=0; i<N_CAL; ++i)
    {
        // number of cycles for 10M nops
        loops_cycles += calibrate()/N_CAL;
    }
    nops_per_frame = (loops_cycles * 17) / FPS;

	// wipe screen
    for (uint32_t i = 0; i < FRAME_SIZE; i++)
    {
	    vga[2*i] = ' ';
	    vga[2*i + 1] = 0x07;
    }	
    // set_cursor(0);

#ifdef DEBUG
	// show first 16B from first 20 sectors
	int first=1010;
    for(int sec=first; sec<first+20; sec++)
    {
    	uint32_t addr = (uint32_t)(0x10000+sec*512);
	    print_hex32(addr, sec-first, 1);
		print_nbr(sec, sec-first, sec<10?11:10);
	    for(int byte=0; byte<16; byte++)
	    {
    	    print_hex8(*((uint8_t*)(addr+byte)), sec-first, 14+3*byte);
    	}
    }
    while(1);
#endif

	// this function never terminate
    print_decompressed(encoded_bin);

	return 0;
}
