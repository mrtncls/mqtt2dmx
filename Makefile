.DEFAULT_GOAL := all

CC = gcc
CFLAGS ?= -fno-common
CFLAGS += -Wall -pthread
CPPFLAGS += -Isrc/app -Isrc/adapters -Isrc/config -Isrc/lighting -Isrc/protocol \
	-I/usr/include/libftdi1
LDLIBS += -lpaho-mqtt3a -lftdi1 -ljansson -pthread

SOURCES := src/app/mqtt2dmx.c src/app/ha.c src/app/shutdown.c \
	src/adapters/dmx.c src/adapters/mqtt.c src/adapters/clock.c \
	src/config/config.c src/config/json_contract.c \
	src/lighting/lights.c src/lighting/transition.c \
	src/protocol/command.c src/protocol/ha_protocol.c src/protocol/ha_topics.c
OBJECTS := $(patsubst src/%.c,build/native/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)

.PHONY: all make build run clean
all make build: mqtt2dmx

mqtt2dmx: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

build/native/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

run: build
	./mqtt2dmx

clean:
	rm -f mqtt2dmx *.o $(OBJECTS) $(DEPS)

-include $(DEPS)