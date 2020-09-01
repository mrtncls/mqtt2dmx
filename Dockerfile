FROM alpine:3.12.0 AS build
WORKDIR /src
COPY . .
RUN apk add --no-cache \
    alpine-sdk \
    libftdi1-dev \
    openssl-dev

# Build mqtt lib
RUN git clone --depth 1 --branch v1.3.1 https://github.com/eclipse/paho.mqtt.c.git
RUN mkdir -p /usr/local/share/man/man1
RUN mkdir -p /usr/local/share/man/man3
RUN cd paho.mqtt.c && make install

RUN make

FROM alpine:3.12.0 AS runtime
COPY --from=build /src/mqtt2dmx /bin/mqtt2dmx
COPY --from=build /usr/local/lib/libpaho* /usr/local/lib/
RUN apk add \
    libftdi1 \
    openssl
# EXPOSE 6454/udp
CMD ["/bin/mqtt2dmx"]
# ENTRYPOINT ["/bin/mqtt2dmx"]