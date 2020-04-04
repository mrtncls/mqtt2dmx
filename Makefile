build:
	# gcc -o artnet2ftdi artnet2ftdi.c $(pkg-config --cflags --libs libftdi1) -Wall
	gcc mqtt2dmx.c -lpaho-mqtt3c -o mqtt2dmx

run: build
	./mqtt2dmx

clean:
	rm mqtt2dmx