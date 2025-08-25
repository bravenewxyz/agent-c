CC = gcc
TARGET = agent-c
SOURCES = main.c json.c agent.c cli.c utils.c

# Optimized build flags for smallest size
CFLAGS_OPT = -std=c99 -Oz -DNDEBUG -ffunction-sections -fdata-sections \
             -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
             -fno-math-errno -ffast-math -fmerge-all-constants -flto \
             -fomit-frame-pointer -fno-ident -fno-stack-check -fno-exceptions \
             -fno-rtti -fno-threadsafe-statics -fvisibility=hidden -fno-builtin \
             -Wl,-dead_strip -Wl,-x -Wl,-S

# Auto-detect platform and build appropriately
all:
	@echo "Detecting platform..."
	@if [ "$$(uname)" = "Darwin" ]; then \
		echo "Building for macOS with GZEXE compression..."; \
		$(MAKE) macos; \
	else \
		echo "Building for Linux with UPX compression..."; \
		$(MAKE) linux; \
	fi

# macOS build with GZEXE compression (7.9KB)
macos: $(SOURCES)
	@echo "Building optimized binary for macOS..."
	$(CC) $(CFLAGS_OPT) -o $(TARGET) $(SOURCES)
	strip -S -x $(TARGET) 2>/dev/null || strip $(TARGET)
	@echo "Applying GZEXE compression..."
	gzexe $(TARGET)
	@echo "✅ macOS build complete: $$(ls -lh $(TARGET) | awk '{print $$5}')"

# Linux build with UPX compression (~16KB)
linux: $(SOURCES)
	@echo "Building optimized binary for Linux..."
	$(CC) $(CFLAGS_OPT) -o $(TARGET) $(SOURCES)
	strip --strip-all $(TARGET) 2>/dev/null || strip $(TARGET)
	@echo "Applying UPX compression..."
	@which upx >/dev/null 2>&1 && upx --best $(TARGET) || echo "⚠️ UPX not found, binary uncompressed"
	@echo "✅ Linux build complete: $$(ls -lh $(TARGET) | awk '{print $$5}')"

clean:
	rm -f $(TARGET) $(TARGET)~ *~

install: all
	cp $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Show help information
help:
	@echo "🚀 Agent-C - A lightweight AI agent written in C"
	@echo ""
	@echo "📋 SIMPLE BUILD COMMANDS:"
	@echo "   make          Auto-detects platform and builds optimally"
	@echo "   make macos    macOS build with GZEXE compression (4.4KB)"
	@echo "   make linux    Linux build with UPX compression (~16KB)"
	@echo "   make clean    Clean all build files"
	@echo "   make install  Install to /usr/local/bin"
	@echo "   make help     Show this help"

.PHONY: all macos linux clean install uninstall help
