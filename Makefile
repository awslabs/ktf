ROOT := $(abspath $(CURDIR))
export ROOT

CC := gcc

COMMON_FLAGS := -I$(ROOT)/include -pipe -MP -MMD -m64 -D__x86_64__

AFLAGS  := $(COMMON_FLAGS) -D__ASSEMBLY__ -nostdlib -nostdinc
CFLAGS  := $(COMMON_FLAGS) -std=gnu99 -O3 -g -Wall -ffreestanding
CFLAGS  += -mno-red-zone -mno-mmx -mno-sse -mno-sse
CFLAGS  += -fno-stack-protector -fno-exceptions -fno-builtin
CFLAGS  += -mcmodel=kernel -fno-pic -fno-asynchronous-unwind-tables -fno-unwind-tables

SOURCES     := $(shell find . -name \*.c)
ASM_SOURCES := $(shell find . -name \*.S)
LINK_SCRIPT := $(shell find . -name \*.ld)

PREP_LINK_SCRIPT := $(LINK_SCRIPT:%.ld=%.lds)

OBJS := $(SOURCES:%.c=%.o)
OBJS += $(ASM_SOURCES:%.S=%.o)

TARGET := kernel64.bin

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "LD " $@
	@ $(CC) $(AFLAGS) -E -P -C -x c $(LINK_SCRIPT) -o $(PREP_LINK_SCRIPT)
	@ ld -T $(PREP_LINK_SCRIPT) -o $@ $^

%.o: %.S
	@echo "AS " $@
	@ $(CC) -c -o $@ $(AFLAGS) $<

%.o: %.c
	@echo "CC " $@
	@ $(CC) -c -o $@ $(CFLAGS) $<

clean:
	@echo "CLEAN"
	@ find $(ROOT) -name \*.d -delete
	@ find $(ROOT) -name \*.o -delete
	@ find $(ROOT) -name \*.lds -delete
	@ find $(ROOT) -name \*.bin -delete
	@ find $(ROOT) -name \*.iso -delete
	@ find $(ROOT) -name cscope.\* -delete

QEMU_PARAMS := -machine q35,accel=kvm -m 1024
QEMU_PARAMS += -display none -vga none -vnc none
QEMU_PARAMS += -debugcon stdio -serial file:/dev/stdout
QEMU_PARAMS += -no-reboot -no-shutdown
QEMU_PARAMS += -enable-kvm
QEMU_PARAMS_KERNEL := -append "param1 param2 param3"
QEMU_PARAMS_DEBUG := -S -s &

ISO_FILE := boot.iso
VHD_FILE := boot.vhd

.PHONY: iso
iso: all
	@echo "GEN ISO" $(ISO_FILE)
	@ grub-file --is-x86-multiboot $(TARGET) || { echo "Multiboot not supported"; exit 1; }
	@ cp $(TARGET) grub/boot/
	@ grub-mkrescue -o $(ISO_FILE) grub 2>> /dev/null
	@ qemu-img convert -f raw -O vpc $(ISO_FILE) $(VHD_FILE)

.PHONY: boot
boot: all iso
	@echo "QEMU START"
	@ sudo qemu-system-x86_64 -cdrom $(ISO_FILE) $(QEMU_PARAMS)

.PHONY: boot_debug
boot_debug: all iso
	sudo qemu-system-x86_64 -cdrom $(ISO_FILE) $(QEMU_PARAMS) $(QEMU_PARAMS_DEBUG)

.PHONY: run
run: all
	sudo "$$QEMU_PATH"/qemu-system-x86_64 -kernel $(TARGET) $(QEMU_PARAMS) $(QEMU_PARAMS_KERNEL)

.PHONY: debug
debug: all
	sudo "$$QEMU_PATH"/qemu-system-x86_64 -kernel $(TARGET) $(QEMU_PARAMS) $(QEMU_PARAMS_KERNEL) $(QEMU_PARAMS_DEBUG)

.PHONY: gdb
gdb: debug
	gdb $(TARGET) -ex 'target remote :1234' -ex 'b _start' -ex 'c'
	sudo killall -9 qemu-system-x86_64

define all_sources
	find $(ROOT) -name "*.[hcsS]"
endef

.PHONY: cscope
cscope:
	@echo "CSCOPE"
	@ $(all_sources) > cscope.files
	@ cscope -b -q -k
