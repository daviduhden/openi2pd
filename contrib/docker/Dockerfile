# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: Dockerfile.
#
# Multi-stage Alpine image that builds i2pd from source and runs it as the
# unprivileged i2pd user with the Docker-specific configuration and entrypoint.
#
# The builder stage needs the -dev packages because it compiles and links the
# router. The runtime stage installs only the shared libraries the linked binary
# loads at start: Boost.JSON, Boost.URL and Boost.Container are compiled
# libraries here, not header-only fallbacks, and OpenSSL, miniupnpc and zlib are
# dynamic dependencies. A package missing from the runtime stage fails when the
# container starts, not when the image is built.
#
# Only the router is built and installed: no tools, no shared library and no
# TUI, because the container has no terminal and no consumer for the local IPC
# socket. Stripping the binary keeps the layer small.
#
# EXPOSE records the ports the image may listen on; it publishes nothing by
# itself. contrib/docker/i2pd-docker.conf decides which services are enabled, so
# pass -p/--publish for the ports the operator actually enables.
#
# /home/i2pd/data is a declared volume and the shipped configuration is copied
# into it, so Docker seeds an empty named volume from the image while a bind
# mount replaces the file with the operator's own. Keys, netDb and torrent state
# therefore survive `docker rm` only when the volume is kept.
#
# USER plus an exec entrypoint leave the router as the container's PID 1, so a
# stop signal reaches it directly. `docker stop` sends SIGTERM, the prompt ordered
# stop, and SIGKILLs after its ten-second default: request the 600-second transit
# drain with SIGINT (`docker kill --signal=INT`) and allow a stop timeout of at
# least 660 seconds.
#

FROM alpine:latest AS builder
RUN apk add --no-cache build-base cmake ninja boost-dev \
      boost-program_options boost-json boost-url boost-container \
      openssl-dev zlib-dev miniupnpc-dev
WORKDIR /src
COPY . .
RUN cmake -S build -B build-output -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DWITH_UPNP=ON \
      -DWITH_LIBRARY=OFF -DI2PD_BUILD_TOOLS=OFF -DI2PD_BUILD_TUI=OFF \
    && cmake --build build-output --parallel "$(nproc)" \
    && strip build-output/i2pd

FROM alpine:latest
LABEL org.opencontainers.image.title="openi2pd"
LABEL org.opencontainers.image.source="https://github.com/daviduhden/openi2pd"
LABEL org.opencontainers.image.licenses="BSD-3-Clause"
# The account is unprivileged and owns the data directory; the group is the
# image's nobody group, which only needs traverse/read on the volume.
RUN apk add --no-cache boost-program_options boost-json boost-url boost-container openssl miniupnpc zlib libstdc++ \
    && adduser -S -h /home/i2pd i2pd \
    && mkdir -p /home/i2pd/data \
    && chown -R i2pd:nobody /home/i2pd
ENV I2PD_HOME="/home/i2pd"
ENV DATA_DIR="/home/i2pd/data"
ENV DEFAULT_ARGS="--datadir=/home/i2pd/data"
COPY --from=builder /src/build-output/i2pd /usr/local/bin/i2pd
# Baked-in certificates make reseed verification work without a network fetch of
# the trust anchors; the entrypoint links this directory into the volume.
COPY --from=builder /src/contrib/certificates /i2pd_certificates
COPY --chown=i2pd:nobody contrib/docker/i2pd-docker.conf /home/i2pd/data/i2pd.conf
COPY --from=builder /src/LICENSE /usr/share/licenses/i2pd/LICENSE
COPY --from=builder /src/NOTICE /usr/share/licenses/i2pd/NOTICE
COPY --chmod=755 contrib/docker/entrypoint.sh /entrypoint.sh
VOLUME /home/i2pd/data
EXPOSE 4444 4447 7656 2827 7654 7650
USER i2pd
ENTRYPOINT ["/entrypoint.sh"]
