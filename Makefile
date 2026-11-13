CC      ?= gcc
CFLAGS  ?= -O2 -march=native
CFLAGS  += -std=c11 -Wall -Wextra -Werror -Wformat=2 \
           -fstack-protector-strong -fstack-clash-protection \
           -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -fPIE
LDFLAGS += -pie -Wl,-z,relro,-z,now

BUILD := build
SRC   := src/vigiad.c src/collect.c src/baseline.c

.PHONY: all debug demo plot clean

all: $(BUILD)/vigiad $(BUILD)/loadgen

$(BUILD)/vigiad: $(SRC) src/collect.h src/baseline.h | $(BUILD)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS) -lm

$(BUILD)/loadgen: tools/loadgen.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -pthread

$(BUILD):
	mkdir -p $(BUILD)

# Build with sanitizers to catch memory errors during development.
# AddressSanitizer does not start on linux-hardened kernels; there use
#   make debug SANITIZE=-fsanitize=undefined
SANITIZE ?= -fsanitize=address,undefined
debug: CFLAGS := -O1 -g $(SANITIZE) -fno-omit-frame-pointer \
                 -std=c11 -Wall -Wextra -Werror
debug: LDFLAGS :=
debug: clean all

# Run the controlled experiment and draw the graph.
demo: all
	./scripts/demo.sh

PYTHON := $(if $(wildcard .venv/bin/python),.venv/bin/python,python3)

plot:
	$(PYTHON) scripts/plot.py data/demo.csv data/demo-events.csv docs/demo.png

clean:
	rm -rf $(BUILD)
