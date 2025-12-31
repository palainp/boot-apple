ENTRY(start)

SECTIONS
{
    /* Kernel loaded at specified address, see mbr.asm */
    . = 0x10000; /* We load everyting at 0x10000, and the MBR is te first sector */

    /* Copy all sections in order as flat binary */
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(COMMON) *(.bss*) }
}
