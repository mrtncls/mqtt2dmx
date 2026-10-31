make: clean mqtt2dmx

CFLAGS ?= -fno-common
CPPFLAGS += -Isrc/app -Isrc/adapters -Isrc/config -Isrc/lighting -Isrc/protocol

mqtt2dmx: mqtt2dmx.o dmx.o mqtt.o shutdown.o command.o lights.o clock.o config.o json_contract.o transition.o ha_protocol.o ha.o
	gcc -o mqtt2dmx mqtt2dmx.o dmx.o mqtt.o shutdown.o command.o lights.o clock.o config.o json_contract.o transition.o ha_protocol.o ha.o -lpaho-mqtt3a -lftdi1 -ljansson -pthread -Wall

mqtt2dmx.o: src/app/mqtt2dmx.c src/app/application.h src/adapters/dmx.h src/adapters/mqtt.h src/protocol/command.h src/lighting/lights.h src/app/shutdown.h src/app/ha.h src/config/config.h src/adapters/clock.h
	gcc $(CPPFLAGS) $(CFLAGS) -c src/app/mqtt2dmx.c -I/usr/include/libftdi1

dmx.o: src/adapters/dmx.c src/adapters/dmx.h src/lighting/lights.h
	gcc $(CPPFLAGS) $(CFLAGS) -c src/adapters/dmx.c -I/usr/include/libftdi1

mqtt.o: src/adapters/mqtt.c src/adapters/mqtt.h
	gcc $(CPPFLAGS) $(CFLAGS) -c src/adapters/mqtt.c -pthread

shutdown.o: src/app/shutdown.c src/app/shutdown.h src/adapters/mqtt.h src/adapters/dmx.h src/adapters/clock.h
	gcc $(CPPFLAGS) $(CFLAGS) -c src/app/shutdown.c

command.o: src/protocol/command.c src/protocol/command.h src/lighting/lights.h
	gcc $(CPPFLAGS) $(CFLAGS) -c src/protocol/command.c

lights.o: src/lighting/lights.c src/lighting/lights.h
	gcc $(CPPFLAGS) $(CFLAGS) -c src/lighting/lights.c

clock.o: src/adapters/clock.c src/adapters/clock.h
	gcc $(CPPFLAGS) $(CFLAGS) -c src/adapters/clock.c

config.o: src/config/config.c src/config/config.h src/config/json_contract.h
	gcc $(CPPFLAGS) $(CFLAGS) -c src/config/config.c

json_contract.o: src/config/json_contract.c src/config/json_contract.h src/config/config.h
	gcc $(CPPFLAGS) $(CFLAGS) -c src/config/json_contract.c

transition.o: src/lighting/transition.c src/lighting/transition.h src/config/config.h
	gcc $(CPPFLAGS) $(CFLAGS) -c src/lighting/transition.c

ha_protocol.o: src/protocol/ha_protocol.c src/protocol/ha_protocol.h src/lighting/transition.h src/config/json_contract.h src/adapters/mqtt.h
	gcc $(CPPFLAGS) $(CFLAGS) -c src/protocol/ha_protocol.c

ha.o: src/app/ha.c src/app/ha.h src/protocol/ha_protocol.h src/adapters/mqtt.h
	gcc $(CPPFLAGS) $(CFLAGS) -c src/app/ha.c

run: build
	./mqtt2dmx

clean:
	rm -f mqtt2dmx *.o