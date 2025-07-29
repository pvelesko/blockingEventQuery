# Auto-detection of Level Zero installation paths
ifeq ($(shell test -d /usr/local/include/level_zero && echo yes),yes)
    ZE_INCLUDE_PATH = /usr/local/include
    ZE_LIB_PATH = /usr/local/lib
else ifeq ($(shell test -d /usr/include/level_zero && echo yes),yes)
    ZE_INCLUDE_PATH = /usr/include
    ZE_LIB_PATH = /usr/lib/x86_64-linux-gnu
else ifeq ($(shell test -d /opt/intel/oneapi/level-zero/latest/include/level_zero && echo yes),yes)
    ZE_INCLUDE_PATH = /opt/intel/oneapi/level-zero/latest/include
    ZE_LIB_PATH = /opt/intel/oneapi/level-zero/latest/lib/intel64
else
    $(error Level Zero headers not found. Please install level-zero-dev package or set ZE_INCLUDE_PATH and ZE_LIB_PATH manually)
endif

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
INCLUDES = -I$(ZE_INCLUDE_PATH)
LIBS = -L$(ZE_LIB_PATH)
LDFLAGS = -lze_loader -lpthread

# Target
TARGET = test_blocking_event_query
SOURCE = test_blocking_event_query.c

# Default target
all: $(TARGET)

$(TARGET): $(SOURCE)
	@echo "Building $(TARGET)..."
	@echo "Using Level Zero include: $(INCLUDES)"
	@echo "Using Level Zero lib: $(LIBS)"
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBS) -o $(TARGET) $(SOURCE) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

# Create a simple SPIR-V kernel if compiler is available (optional)
kernel.spv: kernel.cl
	@if command -v clang >/dev/null 2>&1; then \
		echo "Creating kernel.spv from kernel.cl..."; \
		clang -cc1 -triple spir64 -emit-llvm-bc -o kernel.bc kernel.cl; \
		llvm-spirv kernel.bc -o kernel.spv; \
		rm -f kernel.bc; \
	else \
		echo "clang not found - skipping kernel compilation"; \
	fi

.PHONY: all clean run 