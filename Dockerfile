  
FROM alpine:latest AS build
WORKDIR /src
COPY . .
RUN apk update && apk add --no-cache \
    git \
    gcc \
    make \
    libc-dev \
    libftdi1-dev \
    openssl-dev \
    pkgconfig

# Build mqtt lib
RUN git clone https://github.com/eclipse/paho.mqtt.c.git
RUN mkdir -p /usr/local/share/man/man1
RUN mkdir -p /usr/local/share/man/man3
RUN cd paho.mqtt.c && make install

RUN make

FROM alpine:latest AS runtime
COPY --from=build /src/mqtt2dmx /bin/mqtt2dmx
COPY --from=build /usr/local/lib/libpaho* /usr/local/lib/
RUN apk update
RUN apk add \
    libftdi1 \
    openssl
# EXPOSE 6454/udp
CMD ["/bin/mqtt2dmx"]
# ENTRYPOINT ["/bin/mqtt2dmx"]