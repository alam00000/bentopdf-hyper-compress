FROM gcr.io/oss-fuzz-base/base-builder

RUN git clone --depth 1 \
    https://chromium.googlesource.com/chromium/tools/depot_tools.git \
    /opt/depot_tools
ENV PATH="/opt/depot_tools:${PATH}"

COPY . $SRC/hyper-compress-engine
WORKDIR $SRC/hyper-compress-engine
COPY .clusterfuzzlite/build.sh $SRC/build.sh
