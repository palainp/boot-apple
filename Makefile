# $@ = target file
# $< = first dependency
# $^ = all dependencies

YT_URL := "https://www.youtube.com/watch?v=FtutLA63Cp8"
WIDTH := 80
HEIGHT := 24
FPS := 30

VIDEO := bad-apple.mp4
FRAMES_DIR := frames
ASCII_DIR := ascii_frames
ASCII_FILE := encoded.bin
ASCII_HDR := frames.c
BOOT_IMAGE := os-image.bin
PNG2TXT := ./png2txt
ENCODE := ./encode
DECODE := ./decode
YTDLP := yt-dlp


# First rule is the one executed when no parameters are fed to the Makefile
watch: $(DECODE) $(ASCII_FILE)
	clear
	$(DECODE) $(WIDTH) $(HEIGHT) $(ASCII_FILE)

run: $(BOOT_IMAGE)
	qemu-system-i386 -fda $<

# Boot stuff
$(BOOT_IMAGE): mbr.bin kernel.bin
	# patch mbr.bin with the number of kernel sectors + 1 for mbr
	# offset is the offset for KERNEL_SECTORS (from mbr.asm) in mbr.bin
	# write little-endian (low byte first, high byte second)
	KS=$$(( ( $$(stat -c%s kernel.bin) + 511 ) / 512 )); \
	OFFSET=508; \
	LSB=$$((KS & 0xFF)); \
	MSB=$$(( (KS >> 8) & 0xFF )); \
	echo "Patching mbr.bin: KS=$$KS -> LSB=0x$$(printf %02X $$LSB), MSB=0x$$(printf %02X $$MSB)"; \
	printf "%b" "$$(printf '\\x%02x\\x%02x' $$LSB $$MSB)" | \
		dd of=$< bs=1 seek=$$OFFSET count=2 conv=notrunc; \
	cat $^ > $@

kernel.bin: kernel-entry.o kernel.o frames.o
	ld -m elf_i386 -T linker.ld -nostdlib -o kernel.elf $^
	objcopy -O binary kernel.elf $@

mbr.bin: mbr.asm
	nasm $< -f bin -o $@

kernel-entry.o: kernel-entry.asm
	nasm $< -f elf -o $@

kernel.o: kernel.c $(ASCII_HDR)
	gcc -Os -fno-pie -m32 -ffreestanding -DSCREEN_WIDTH=$(WIDTH) -DSCREEN_HEIGHT=$(HEIGHT) -DFPS=$(FPS) -c $< -o $@

%.o: %.c
	gcc -fno-pie -m32 -ffreestanding -Wno-unused-parameter -c $< -o $@

# Preprocessing stuff
$(VIDEO):
	$(YTDLP) -f mp4 -o "$(VIDEO)" "$(YT_URL)"
	
$(FRAMES_DIR): $(VIDEO)
	mkdir -p $(FRAMES_DIR)
	ffmpeg -r $(FPS) -i $(VIDEO) -vsync 0 $(FRAMES_DIR)/frame_%05d.png
	touch $(FRAMES_DIR)

$(PNG2TXT): $(PNG2TXT).c
	gcc -lm $< -o $@
	chmod +x $@

$(ENCODE): $(ENCODE).c
	gcc -lm $< -o $@
	chmod +x $@

$(DECODE): $(ENCODE)
	ln -sf $< $@

$(ASCII_DIR): $(PNG2TXT) $(FRAMES_DIR)
	mkdir -p $(ASCII_DIR)
	find $(FRAMES_DIR) -type f -name '*.png' | while read f; do \
		base=$$(basename $$f .png); \
		$(PNG2TXT) $(WIDTH) $(HEIGHT) $$f > $(ASCII_DIR)/$$base.txt; \
	done
	touch $(ASCII_DIR)

$(ASCII_FILE): $(ENCODE) $(ASCII_DIR)
	rm -f $(ASCII_FILE)
	find $(ASCII_DIR) -type f -name '*.txt' | sort | head -n 3000 | while read f; do \
		$(ENCODE) $(WIDTH) $(HEIGHT) $$f tmp.bin ; \
		cat tmp.bin >> $(ASCII_FILE) ; \
	done
	rm tmp.bin

$(ASCII_HDR): $(ASCII_FILE)
	xxd -i $< > $@

clean:
	$(RM) -rf *.bin *.o *.dis kernel.elf $(ASCII_HDR)

clean-all: clean
	$(RM) -rf $(ASCII_DIR) $(FRAMES_DIR) $(ASCII_FILE) $(ENCODE) $(DECODE) $(PNG2TXT) $(VIDEO)
