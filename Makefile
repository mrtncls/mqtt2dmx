make: clean mqtt2dmx

mqtt2dmx: mqtt2dmx.o dmx.o mqtt.o
	gcc -o mqtt2dmx mqtt2dmx.o dmx.o mqtt.o -lpaho-mqtt3a -lftdi1 -Wall

mqtt2dmx.o:
	gcc -c mqtt2dmx.c -I/usr/include/libftdi1

dmx.o:
	gcc -c dmx.c -I/usr/include/libftdi1

mqtt.o:
	gcc -c mqtt.c

run: build
	./mqtt2dmx

clean:
	rm -f mqtt2dmx *.o