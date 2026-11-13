FROM ubuntu:24.04 AS native
RUN if [ "$(dpkg --print-architecture)" = amd64 ]; then \
      printf '%s\n' http://mirrors.edge.kernel.org/ubuntu/ http://mirrors.mit.edu/ubuntu/ http://mirror.math.princeton.edu/pub/ubuntu/ http://azure.archive.ubuntu.com/ubuntu/ http://archive.ubuntu.com/ubuntu/ > /etc/apt/mirrors.txt; \
    else \
      printf '%s\n' http://ports.ubuntu.com/ubuntu-ports/ http://mirrors.mit.edu/ubuntu-ports/ > /etc/apt/mirrors.txt; \
    fi \
    && sed -i 's#^URIs: .*#URIs: mirror+file:/etc/apt/mirrors.txt#' /etc/apt/sources.list.d/ubuntu.sources \
    && printf 'Acquire::Retries "5";\nAcquire::http::Timeout "20";\n' > /etc/apt/apt.conf.d/80-retries
RUN apt-get update && apt-get install -y --no-install-recommends \
    git curl ca-certificates python3 xz-utils lsb-release file \
    build-essential cmake ninja-build pkg-config rsync \
    wget gnupg software-properties-common \
    && if [ "$(dpkg --print-architecture)" = arm64 ]; then \
      curl -fsSL -o /tmp/llvm.sh https://apt.llvm.org/llvm.sh && bash /tmp/llvm.sh 23; \
    fi \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 \
    https://chromium.googlesource.com/chromium/tools/depot_tools.git /opt/depot_tools
ENV PATH="/opt/depot_tools:${PATH}" DEPOT_TOOLS_UPDATE=0
WORKDIR /engine
COPY core/build/apply-tree.sh core/build/apply-tree.sh
RUN mkdir -p core/build/gsrc && cd core/build/gsrc \
    && PIN="$(grep '^PDFIUM_PIN=' ../apply-tree.sh | cut -d= -f2)" \
    && if [ "$(dpkg --print-architecture)" = arm64 ]; then \
      GN="$(curl -fsSL "https://pdfium.googlesource.com/pdfium/+/$PIN/DEPS?format=TEXT" | base64 -d | grep -o "'gn_version': '[^']*'" | cut -d"'" -f4)" \
      && printf '%s\n' 'solutions = [{"name": "pdfium", "url": "https://pdfium.googlesource.com/pdfium.git", "managed": False,' \
        "  \"custom_vars\": {\"reclient_package\": \"gn/gn/\", \"reclient_version\": \"$GN\"}}]" > .gclient; \
    else \
      gclient config --unmanaged https://pdfium.googlesource.com/pdfium.git; \
    fi \
    && gclient sync --revision "pdfium@$PIN" -D --no-history
COPY . .
RUN docker/build-jpegli-linux.sh && docker/build-linux.sh
RUN curl -fsSL https://sh.rustup.rs | sh -s -- -y --profile minimal \
    && . "$HOME/.cargo/env" \
    && cargo build --release --locked -p hyper-compress-cli -p hyper-compress-server --manifest-path rust/Cargo.toml \
    && cp rust/target/release/hyper rust/target/release/hyper-server cli/prebuilt/

FROM node:22-slim AS app
WORKDIR /engine
COPY package.json package-lock.json tsconfig.json ./
COPY sdk sdk
COPY cli/bin cli/bin
COPY scripts/smoke.ts scripts/
COPY wasm/glue.ts wasm/
COPY tests/cli.test.ts tests/engine.integration.test.ts tests/errors.test.ts tests/helpers.ts tests/options.test.ts tests/pdfa.test.ts tests/
RUN npm ci --no-audit --no-fund && npm run build && npm prune --omit=dev

FROM debian:trixie-slim
RUN apt-get update && apt-get upgrade -y \
    && apt-get install -y --no-install-recommends qpdf \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 1000 --create-home hyper
WORKDIR /engine
COPY --from=app /engine/dist/sdk/node dist/sdk/node
COPY web/index.html web/options-ui.js web/app.js web/app.css web/engine-http.js web/
COPY web/fonts web/fonts
COPY web/icons web/icons
COPY docker/entrypoint.sh /entrypoint.sh
COPY --from=native /engine/cli/prebuilt/hpdf-worker /engine/cli/prebuilt/hpdf-render /engine/cli/prebuilt/hyper /engine/cli/prebuilt/hyper-server cli/prebuilt/
ENV HYPER_QPDF=/usr/bin/qpdf
USER hyper
EXPOSE 8080
WORKDIR /work
ENTRYPOINT ["/entrypoint.sh"]
CMD ["serve"]
