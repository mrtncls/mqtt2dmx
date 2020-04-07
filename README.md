# MQTT to FTDI DMX gateway

docker run --name mqtt2dmx --rm --privileged -v /dev/bus/usb:/dev/bus/usb -e MQTT_ADDRESS=192.168.1.10:1883 mrtncls/mqtt2dmx

## Environment variables

MQTT_ADDRESS: required
MQTT_CLIENTID: optional, default mqtt2dmx
MQTT_TOPIC: optional, default dmx/channels/+/set

## MQTT topic

Publish message to dmx/channels/16/set with payload 199.
A dmx frame with value 199 will be send to channel 16.
A confirmation will be published to dmx/channels/16 with payload 199.