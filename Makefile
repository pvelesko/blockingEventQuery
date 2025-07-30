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

all:
	/space/pvelesko/chipStar/NoFence/build/bin/hipcc.bin -g -o test ./reproduce_failure.hip

run:
	./test

trace:
	iprof -m full --no-analysis ./test ; iprof -t -r > original.trace
	

reproduce:
	gcc -g -I$(ZE_INCLUDE_PATH) ./ze_trace_replica.c $(LIBS) $(LDFLAGS) -ldl -o ze_trace_replica
	timeout 10 ./ze_trace_replica || echo "Command timed out or failed"

reproduce_traced:
	gcc -g -I$(ZE_INCLUDE_PATH) ./ze_trace_replica.c $(LIBS) $(LDFLAGS) -ldl -o ze_trace_replica
	iprof -m full --no-analysis ./ze_trace_replica ; iprof -t -r > reproducer.trace

clean:
	rm -f test reproducer original.trace reproducer.trace