[bits 16]
[org 0x7c00]

; Where to load the kernel to (we read all the disk, so
; the first sector is MBR, and we will jump into the
; kernel after one 512 bytes sector...)
KERNEL_ENTRY equ 0x10000
KERNEL_SEG equ 0x1000
KERNEL_OFF equ 0

SECTOR_PER_TRACK equ 18
TOTAL_HEADS equ 2

; BIOS sets boot drive in 'dl'; store for later use
mov [boot_drive], dl

mov bp, 0x9000                  ; Setup stack
mov sp, bp

call load_kernel
call switch_to_32bit

%include "gdt.asm"
%include "switch-to-32bit.asm"

[bits 16]
error:
    mov si, error_msg
    call print_string
    jmp $

; we consider to have ax as spare register, so don't bother to push/pop...
print_dot:
    mov al, '.'
    ; fall through

; Print char in al, auto-increase [position] 
print_char:
	push cx
	push es

    mov di, [position]
    add word [position], 2
    mov ah, 0x07
    mov cx, 0xb800
    mov es, cx
    stosw

	pop es
	pop cx
    ret

; Print a null-terminated string at address pointed by SI
print_string:
.next_char:
    lodsb                       ; load byte from [SI] into AL and increment SI
    cmp al, 0
    je .done                    ; if NULL, finish
    call print_char
    jmp .next_char
.done:
    ret

load_kernel:
	; get sectors/head/cylinder informations
	mov ah, 0x08
	mov dl, [boot_drive]
	int 0x13
	jc error
	; CH/CL/DH have cyl/sect/head
	inc ch ; sectors are in [1-ch]
	mov [max_cylinders], ch
    inc cl
	mov [max_sectors], cl
    inc dh
	mov [max_heads], dh

    mov si, boot_msg
    call print_string           ; Print boot message

	mov dl, [boot_drive]

    ; es:bx = buffer pointer is set as input as well
	mov ax, KERNEL_SEG
	mov es, ax
	mov bx, KERNEL_OFF

	mov ch, 0  ; initial cylinder
	mov dh, 0  ; initial head
    mov cl, 2  ; initial sector (i.e. skip MBR sector)

.load_loop:
	call print_dot

	; as we always write ax, we can use it as spare register
	; so we need to always rewrite it to correct values
    mov ah, 0x02                ; read mode
	mov al, 1  ; number of sector to read

	; es:bx is destination
	; dl/ch/dh are drive/cylinder/head
	; cl is the sector to read

    int 0x13
    jc error

    cmp al, 1 ; BIOS sets 'al' to the # of sectors actually read
               ; compare it to 1 and error out if !=
    jne error
	
	; okay, read was fine, update es:bx
	add bx, 0x200 ; update destination address by 512
	jnc short .no_bx_overflow
	; bx overflow, so increase es
	clc
	mov ax, es
	add ax, 0x1000
	mov es, ax

.no_bx_overflow:
	; Now increase cl (with eventually cylinder+head)
	inc cl
	cmp cl, [max_sectors]
	jne .1 ; no head/cylinder change needed

	mov cl, 1 ; restart with first sector of next head

	; move to next head/cylinder
	inc dh
	cmp dh, [max_heads]
	jne .1

	xor dh, dh ; restart with first head of new cylinder
	inc ch
	; We have a floppy, we never overflow the cylinder ;)

.1:
	dec word [total_sectors]
    jnz .load_loop              ; More sectors to read?

    mov al, '!'
    ;jmp $
    jmp print_char              ; Print '!' when loading is finished
    ; Fall through print_char

[bits 32]
BEGIN_32BIT:
    call KERNEL_ENTRY           ; Give control to the kernel
    jmp $                       ; Loop in case kernel returns

; Where to read data from
boot_drive db 0                 ; Boot drive variable
max_sectors db 0
max_heads db 0
max_cylinders db 0

position dw 0                   ; Position of the character, for boot messages :)
boot_msg db "Loading Kernel ",0
error_msg db "Panic /!\ :( ",0


times 508 - ($-$$) db 0         ; Padding

; Number of kernel sectors to load (16-bit word, little endian)
; That variable need to be patched once kernel.bin is compiled
total_sectors dw 0xcafe

dw 0xaa55                       ; Magic number
