BuildDirectory := ../.Build
EFIRoot        := $(BuildDirectory)/EFI
ImgSysMirror   := .Build
BootImg        := $(BuildDirectory)/BootImg.img

SubMake := $(shell find . -name Makefile \
    -not -path './Makefile' \
    -not -path './PCIBus/*' \
	-not -path './ELFLoader/*' \
	-not -path './Tty/*')

.PHONY: all clean BootImg BuildModules

all: BuildModules BootImg

BuildModules:
	@for mk in $(SubMake); do \
		echo ">>> Building in $$(dirname $$mk)"; \
		$(MAKE) -C $$(dirname $$mk); \
	done
BootImg:
	@mkdir -p $(BuildDirectory)
	@(cd $(ImgSysMirror) && find . | cpio -o -H newc) > $(BootImg)
	@cp $(BootImg) $(EFIRoot)/
	
clean:
	@rm -rf $(BuildDirectory)/BootImg.img
	@for mk in $(SubMake); do \
		$(MAKE) -C $$(dirname $$mk) clean; \
	done