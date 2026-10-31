FROM ruby:3.3.6-bookworm@sha256:347edd0c70ee08d87de9f01b99de2f14a64cedb5d1bfb38457dfe8cd0bf113c5 AS test

RUN apt-get update && apt-get install -y --no-install-recommends \
    libftdi1-dev=1.5-6+b2 \
    libjansson-dev=2.14-2 \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 --branch v1.3.1 \
    https://github.com/eclipse/paho.mqtt.c.git /opt/paho \
    && test "$(git -C /opt/paho rev-parse HEAD)" = d34c51214f4172f2e12bb17532c9f44f72a57dd4

WORKDIR /workspace
COPY Gemfile Gemfile.lock ./
RUN bundle config set frozen true && bundle install

COPY . .
ENTRYPOINT ["ruby", "tests/run.rb"]
CMD ["test:all"]

FROM test AS test-results
RUN ruby tests/run.rb test:all && touch /test-passed

FROM alpine:3.12.0 AS build
WORKDIR /src
RUN apk add --no-cache \
    alpine-sdk \
    libftdi1-dev \
    jansson-dev \
    openssl-dev

# Build mqtt lib
RUN git clone --depth 1 --branch v1.3.1 https://github.com/eclipse/paho.mqtt.c.git
RUN mkdir -p /usr/local/share/man/man1
RUN mkdir -p /usr/local/share/man/man3
RUN cd paho.mqtt.c && make install

COPY Makefile ./
COPY src/ ./src/
# Keep the test stage on the production build's dependency path.
COPY --from=test-results /test-passed /tmp/test-passed
RUN make

FROM alpine:3.12.0 AS runtime
COPY --from=build /src/mqtt2dmx /bin/mqtt2dmx
COPY --from=build /usr/local/lib/libpaho* /usr/local/lib/
RUN apk add \
    libftdi1 \
    jansson \
    openssl
# EXPOSE 6454/udp
CMD ["/bin/mqtt2dmx"]
# ENTRYPOINT ["/bin/mqtt2dmx"]