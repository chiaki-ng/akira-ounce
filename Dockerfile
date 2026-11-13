ARG BASE=devkita64-ounce:20261113f
FROM ${BASE}

RUN apt-get update && apt-get install -y --no-install-recommends ninja-build jq && \
    rm -rf /var/lib/apt/lists/*

RUN dkp-pacman -Sy --noconfirm && \
    dkp-pacman -S --noconfirm --needed switch-mbedtls && \
    dkp-pacman -Scc --noconfirm && \
    test "$(readlink "${DEVKITPRO}/libnx")" = libounce

COPY scripts/docker /opt/akira-docker
COPY --from=ounce-deps . /tmp/ounce-deps
RUN bash /opt/akira-docker/install-ounce-deps.sh /tmp/ounce-deps && rm -rf /tmp/ounce-deps
