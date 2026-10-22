# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: Dockerfile.
#
# Multi-stage Alpine image that builds i2pd from source and runs it as the
# unprivileged i2pd user with the Docker-specific configuration and entrypoint.
#

FROM alpine:latest AS builder
RUN apk add --no-cache build-base cmake ninja boost-dev openssl-dev zlib-dev miniupnpc-dev
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
RUN apk add --no-cache boost-program_options openssl miniupnpc zlib libstdc++ \
    && adduser -S -h /home/i2pd i2pd \
    && mkdir -p /home/i2pd/data \
    && chown -R i2pd:nobody /home/i2pd
ENV I2PD_HOME="/home/i2pd"
ENV DATA_DIR="/home/i2pd/data"
ENV DEFAULT_ARGS="--datadir=/home/i2pd/data"
COPY --from=builder /src/build-output/i2pd /usr/local/bin/i2pd
COPY --from=builder /src/contrib/certificates /i2pd_certificates
COPY --chown=i2pd:nobody contrib/docker/i2pd-docker.conf /home/i2pd/data/i2pd.conf
COPY --chmod=755 contrib/docker/entrypoint.sh /entrypoint.sh
VOLUME /home/i2pd/data
EXPOSE 4444 4447 7656 2827 7654 7650
USER i2pd
ENTRYPOINT ["/entrypoint.sh"]
