FROM gcr.io/oss-fuzz-base/base-builder

RUN apt-get update && \
    apt-get install -y --no-install-recommends software-properties-common && \
    add-apt-repository -y ppa:git-core/ppa && \
    apt-get update && \
    apt-get install -y --no-install-recommends git zip pkg-config && \
    rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 \
    https://chromium.googlesource.com/chromium/tools/depot_tools.git \
    /opt/depot_tools
ENV PATH="/opt/depot_tools:${PATH}"
ENV DEPOT_TOOLS_UPDATE=0
ENV DEPOT_TOOLS_METRICS=0

COPY . $SRC/hyper-compress-engine
WORKDIR $SRC/hyper-compress-engine
COPY .clusterfuzzlite/build.sh $SRC/build.sh
