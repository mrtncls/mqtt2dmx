make: clean mqtt2dmx

CFLAGS ?= -fno-common

mqtt2dmx: mqtt2dmx.o dmx.o mqtt.o shutdown.o command.o lights.o clock.o
	gcc -o mqtt2dmx mqtt2dmx.o dmx.o mqtt.o shutdown.o command.o lights.o clock.o -lpaho-mqtt3a -lftdi1 -pthread -Wall

mqtt2dmx.o: src/mqtt2dmx.c src/application.h src/dmx.h src/mqtt.h src/command.h src/lights.h src/shutdown.h
	gcc $(CFLAGS) -c src/mqtt2dmx.c -I/usr/include/libftdi1

dmx.o: src/dmx.c src/dmx.h src/lights.h
	gcc $(CFLAGS) -c src/dmx.c -I/usr/include/libftdi1

mqtt.o: src/mqtt.c src/mqtt.h
	gcc $(CFLAGS) -c src/mqtt.c -pthread

shutdown.o: src/shutdown.c src/shutdown.h src/mqtt.h src/dmx.h src/clock.h
	gcc $(CFLAGS) -c src/shutdown.c

command.o: src/command.c src/command.h src/lights.h
	gcc $(CFLAGS) -c src/command.c

lights.o: src/lights.c src/lights.h
	gcc $(CFLAGS) -c src/lights.c

clock.o: src/clock.c src/clock.h
	gcc $(CFLAGS) -c src/clock.c

run: build
	./mqtt2dmx

clean:
	rm -f mqtt2dmx *.o