CC = gcc
TARGET = agent-c
SOURCE = agent.c

# Detect OS once
UNAME := $(shell uname)

# Optimized build flags for smallest compressed size
CFLAGS_OPT = -std=c99 -D_POSIX_C_SOURCE=200809L -Os -DNDEBUG \
             -ffunction-sections -fdata-sections \
             -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
             -fno-math-errno -fmerge-all-constants -flto \
             -fomit-frame-pointer -fno-ident -fno-stack-check \
             -fvisibility=hidden -fno-builtin

# Linker flags per-OS
ifeq ($(UNAME),Darwin)
LDFLAGS_OPT = -Wl,-dead_strip -Wl,-x -Wl,-S
else
LDFLAGS_OPT = -Wl,--gc-sections
endif

# Self-extracting archive header (LZMA on macOS, UPX on Linux)
define SFX_HEADER
#!/bin/sh
tail -c +83 "$$0"|lzma -dc>/tmp/a$$$$;chmod +x /tmp/a$$$$;exec /tmp/a$$$$ "$$@"
endef

all:
	@if [ "$$(uname)" = "Darwin" ]; then \
		$(MAKE) macos; \
	else \
		$(MAKE) linux; \
	fi

# macOS build with LZMA self-extracting archive (~2.5KB)
macos: $(SOURCE)
	@echo "Building optimized binary..."
	$(CC) $(CFLAGS_OPT) -o $(TARGET).bin $(SOURCE) $(LDFLAGS_OPT) -framework Security -framework CoreFoundation
	strip -S -x $(TARGET).bin
	@printf '#!/bin/sh\ntail -c +83 "$$0"|lzma -dc>/tmp/a$$$$;chmod +x /tmp/a$$$$;exec /tmp/a$$$$ "$$@"\n' > $(TARGET)
	lzma -9 < $(TARGET).bin >> $(TARGET)
	chmod +x $(TARGET)
	rm -f $(TARGET).bin
	@echo "Built: $$(wc -c < $(TARGET) | tr -d ' ') bytes"

# Linux build with UPX compression
linux: $(SOURCE)
	@echo "Building optimized binary..."
	$(CC) $(CFLAGS_OPT) -o $(TARGET) $(SOURCE) $(LDFLAGS_OPT)
	strip --strip-all $(TARGET) 2>/dev/null || strip $(TARGET)
	@which upx >/dev/null 2>&1 && upx --best $(TARGET) || echo "UPX not found"
	@echo "Built: $$(wc -c < $(TARGET) | tr -d ' ') bytes"

clean:
	rm -f $(TARGET) $(TARGET).bin $(TARGET)~ *~

install: all
	cp $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)

help:
	@echo "Agent-C - AI agent in C"
	@echo ""
	@echo "  make          Auto-detect platform and build"
	@echo "  make macos    macOS build with LZMA SFX (~2.5KB)"
	@echo "  make linux    Linux build with UPX"
	@echo "  make clean    Clean build files"
	@echo "  make install  Install to /usr/local/bin"

.PHONY: all macos linux clean install uninstall help
