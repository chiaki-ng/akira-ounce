ARG BASE=docker.io/devkitpro/toolchain-base:20250727@sha256:2a489c022aa4de3b302eff99c9088a3d4f341d114b35930f365ed79c020dca4a
FROM ${BASE}

ARG OUNCE_DEVKIT_VERSION

ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITA64=/opt/devkitpro/devkitA64
ENV PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/portlibs/switch2/bin:/opt/devkitpro/tools/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

RUN apt-get update && \
    apt-get install -y --no-install-recommends glslang-tools && \
    rm -rf /var/lib/apt/lists/*

RUN dkp-pacman -Syyu --noconfirm && \
    dkp-pacman -S --needed --noconfirm \
        devkitA64 \
        devkita64-rules \
        dkp-toolchain-vars \
        devkit-env \
        devkita64-cmake \
        switch-cmake \
        switch-tools \
        general-tools && \
    dkp-pacman -Scc --noconfirm && \
    test ! -e "${DEVKITPRO}/libnx" && \
    test ! -e "${DEVKITPRO}/portlibs/switch/lib"

COPY --from=ounce-devkit libounce /opt/devkitpro/libounce
COPY --from=ounce-devkit portlibs/switch2 /opt/devkitpro/portlibs/switch2
COPY --from=ounce-devkit switch2vars.sh ounce-devkit-version.txt /opt/devkitpro/
RUN grep -qx "ounce-devkit ${OUNCE_DEVKIT_VERSION}" "${DEVKITPRO}/ounce-devkit-version.txt" && \
    test -f "${DEVKITPRO}/libounce/lib/libounce.a" && \
    ln -s libounce "${DEVKITPRO}/libnx"

LABEL org.opencontainers.image.title="devkita64-ounce"
CMD ["bash"]
