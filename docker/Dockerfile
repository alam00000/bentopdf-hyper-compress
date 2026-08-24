FROM ubuntu:24.04 AS native
RUN apt-get update && apt-get install -y --no-install-recommends \
    git curl ca-certificates python3 xz-utils lsb-release file \
    build-essential cmake ninja-build pkg-config rsync \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 \
    https://chromium.googlesource.com/chromium/tools/depot_tools.git /opt/depot_tools
ENV PATH="/opt/depot_tools:${PATH}" DEPOT_TOOLS_UPDATE=0
WORKDIR /engine
COPY core/build/apply-tree.sh core/build/apply-tree.sh
RUN mkdir -p core/build/gsrc && cd core/build/gsrc \
    && PIN="$(grep '^PDFIUM_PIN=' ../apply-tree.sh | cut -d= -f2)" \
    && gclient config --unmanaged https://pdfium.googlesource.com/pdfium.git \
    && gclient sync --revision "pdfium@$PIN" -D --no-history
COPY . .
RUN docker/build-jpegli-linux.sh && docker/build-linux.sh

FROM node:22-slim AS app
WORKDIR /engine
COPY package.json package-lock.json tsconfig.json ./
COPY sdk sdk
COPY cli/bin cli/bin
COPY scripts/smoke.ts scripts/
COPY wasm/glue.ts wasm/
COPY server server
COPY tests/cli.test.ts tests/engine.integration.test.ts tests/errors.test.ts tests/helpers.ts tests/options.test.ts tests/pdfa.test.ts tests/
RUN npm ci --no-audit --no-fund && npm run build && npm prune --omit=dev

FROM node:22-slim
RUN apt-get update && apt-get install -y --no-install-recommends qpdf \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /engine
COPY --from=app /engine/dist dist
COPY --from=app /engine/node_modules node_modules
COPY --from=app /engine/package.json package.json
COPY server/index.html dist/server/index.html
COPY web/options-ui.js web/options-ui.js
COPY docker/entrypoint.sh /entrypoint.sh
COPY --from=native /engine/cli/prebuilt/hpdf-worker /engine/cli/prebuilt/hpdf-render cli/prebuilt/
ENV HYPER_QPDF=/usr/bin/qpdf
USER node
EXPOSE 8080
WORKDIR /work
ENTRYPOINT ["/entrypoint.sh"]
CMD ["serve"]
