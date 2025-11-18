Compiler       := gcc

Cflg := \
-Wall \
-Wextra \
-O2 \
-ffreestanding \
-fno-stack-protector \
-mno-red-zone
Lflg := \
-nostdlib \
-static

Finale   := ../BootImg/.Build

Src      := $(shell find . -type f -name '*.c')
SrcElfs     := $(patsubst ./%.c,$(Finale)/%.elf,$(Src))

# Default target
all: $(SrcElfs)

$(Finale)/%.elf: ./%.c
	@mkdir -p $(dir $@)
	$(Compiler) $(Cflg) $< -o $@ $(Lflg)

clean:
	rm -rf $(Finale)

.PHONY: \
all \
clean