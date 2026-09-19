FROM ubuntu:22.04

LABEL org.opencontainers.image.source="https://github.com/notnullai/rift"
LABEL org.opencontainers.image.description="Rift — passive Q16.16 SNN anomaly detector"

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /rift

COPY bin/rift_replay-linux-x86_64 /usr/local/bin/rift_replay
COPY data/ /rift/data/
COPY verify.sh /usr/local/bin/verify.sh

RUN chmod +x /usr/local/bin/rift_replay /usr/local/bin/verify.sh

ENTRYPOINT ["/usr/local/bin/verify.sh"]
