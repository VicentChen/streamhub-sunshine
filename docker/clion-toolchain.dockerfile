# syntax=docker/dockerfile:1
ARG BASE=debian
ARG TAG=trixie-slim
FROM ${BASE}:${TAG} AS toolchain-base

ENV DEBIAN_FRONTEND=noninteractive

FROM toolchain-base AS toolchain

ARG TARGETPLATFORM
RUN echo "target_platform: ${TARGETPLATFORM}"

ENV DISPLAY=:0

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# install dependencies
RUN <<_DEPS
#!/bin/bash
set -e
apt-get update -y
apt-get install -y --no-install-recommends \
  build-essential \
  cmake=3.31.* \
  ca-certificates \
  doxygen \
  gcc=4:14.2.* \
  g++=4:14.2.* \
  gdb \
  git \
  graphviz \
  libcurl4-openssl-dev \
  libglib2.0-dev \
  libminiupnpc-dev \
  libopus-dev \
  libssl-dev \
  npm \
  wget
apt-get clean
rm -rf /var/lib/apt/lists/*
_DEPS


WORKDIR /toolchain
RUN <<_ENTRYPOINT
#!/bin/bash
set -e
cat <<EOF > entrypoint.sh
#!/bin/bash
if [ "\$#" -eq 0 ]; then
  exec "/bin/bash"
else
  exec "\$@"
fi
EOF

# Make the script executable
chmod +x entrypoint.sh

# Note about CLion
echo "ATTENTION: CLion will override the entrypoint, you can disable this in the toolchain settings"
_ENTRYPOINT

# Use the shell script as the entrypoint
ENTRYPOINT ["/toolchain/entrypoint.sh"]
