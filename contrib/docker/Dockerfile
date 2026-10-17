FROM alpine:latest AS builder
RUN apk add --no-cache build-base boost-dev openssl-dev zlib-dev miniupnpc-dev
WORKDIR /src
COPY . .
RUN make -j$(nproc) USE_UPNP=yes DEBUG=no && strip i2pd

FROM alpine:latest
LABEL org.opencontainers.image.title="Open I2Pd"
LABEL org.opencontainers.image.source="https://github.com/daviduhden/open-i2pd"
LABEL org.opencontainers.image.licenses="BSD-3-Clause"
RUN apk add --no-cache boost-filesystem boost-system boost-program_options boost-date_time boost-thread boost-iostreams boost-json boost-url openssl miniupnpc libstdc++ \
    && adduser -S -h /home/i2pd i2pd \
    && mkdir -p /home/i2pd/data \
    && chown -R i2pd:nobody /home/i2pd
ENV I2PD_HOME="/home/i2pd"
ENV DATA_DIR="/home/i2pd/data"
ENV DEFAULT_ARGS="--datadir=/home/i2pd/data"
COPY --from=builder /src/i2pd /usr/local/bin/i2pd
COPY --from=builder /src/contrib/certificates /i2pd_certificates
COPY --chown=i2pd:nobody contrib/docker/i2pd-docker.conf /home/i2pd/data/i2pd.conf
COPY --chmod=755 contrib/docker/entrypoint.sh /entrypoint.sh
VOLUME /home/i2pd/data
EXPOSE 7070 4444 4447 7656 2827 7654 7650
USER i2pd
ENTRYPOINT ["/entrypoint.sh"]
