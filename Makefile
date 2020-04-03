build :
	gcc mqtt2dmx.c -lpaho-mqtt3c -o mqtt2dmx

run : build
	./mqtt2dmx

clean :
	rm mqtt2dmx

deps :
	sudo apt-get install gcc make libssl-dev
	git clone https://github.com/eclipse/paho.mqtt.c.git
	cd paho.mqtt.c/
	sudo make install