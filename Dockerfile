# syntax=docker/dockerfile:1
# fritzing-render: the CLI and the MCP server, with Fritzing's parts
# libraries baked in.
#
#   docker run -i --rm ghcr.io/leonfedotov/fritzing-render                 # MCP server on stdio
#   docker run --rm -v "$PWD:/work" ghcr.io/leonfedotov/fritzing-render \
#     render sketch.fzz -o sketch.svg --png sketch.png                     # CLI

# The fritzing-app commit whose sources are compiled in, and whose resources
# (wire colors, LED colors, pin header templates, schematic symbols, fonts)
# are used at run time.
ARG FRITZING_APP_REF=5aa56a510183c23084990a6b4481708cad24c15b

# Parts libraries and svgpp: the same for every platform, so fetched once.
FROM --platform=$BUILDPLATFORM debian:trixie-slim AS vendor
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl git perl unzip \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /opt/fritzing-render
COPY scripts/fetch-vendor.sh scripts/prune-vendor.sh scripts/
# The submodules' part archives (check out with --recurse-submodules)
COPY libraries/adafruit/parts libraries/adafruit/parts
COPY libraries/sparkfun/products libraries/sparkfun/products
COPY libraries/mgesteiro libraries/mgesteiro
# libraries/seeed is left out: its repository states no licence to redistribute it.
RUN scripts/fetch-vendor.sh && scripts/prune-vendor.sh

FROM --platform=$BUILDPLATFORM debian:trixie-slim AS fritzing-app
ARG FRITZING_APP_REF
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates git \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /opt/fritzing-app
RUN git init -q \
 && git remote add origin https://github.com/fritzing/fritzing-app.git \
 && git sparse-checkout set --no-cone /src /resources /sketches/core/AnalogInputPot.fzz \
 && git fetch -q --depth 1 --filter=blob:none origin "$FRITZING_APP_REF" \
 && git checkout -q FETCH_HEAD \
 && rm -rf .git

FROM --platform=$BUILDPLATFORM node:24-trixie-slim AS mcp
WORKDIR /opt/fritzing-render/mcp
RUN npm install -g pnpm@12.5.1 > /dev/null
COPY mcp/package.json mcp/pnpm-lock.yaml mcp/pnpm-workspace.yaml ./
RUN pnpm install --prod --frozen-lockfile
COPY mcp/src src

FROM debian:trixie-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      cmake g++ make libboost-dev qt6-base-dev qt6-base-private-dev qt6-svg-dev \
      fontconfig fonts-dejavu-core \
 && rm -rf /var/lib/apt/lists/*
COPY --from=fritzing-app /opt/fritzing-app /opt/fritzing-app
COPY --from=vendor /opt/fritzing-render/vendor /opt/fritzing-render/vendor
WORKDIR /opt/fritzing-render
COPY CMakeLists.txt ./
COPY src src
COPY tests tests
COPY libraries/fritzing-parts-extra libraries/fritzing-parts-extra
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFRITZING_APP=/opt/fritzing-app -DQT_NO_PRIVATE_MODULE_WARNING=ON > /dev/null \
 && cmake --build build -j"$(nproc)" \
 && (cd build && ctest --output-on-failure)

FROM node:24-trixie-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      libqt6core6t64 libqt6gui6 libqt6widgets6 libqt6svg6 libqt6xml6 qt6-qpa-plugins \
      fontconfig fonts-dejavu-core \
 && rm -rf /var/lib/apt/lists/*
# What the renderer reads from fritzing-app at run time, at the path it was built with.
COPY --from=fritzing-app /opt/fritzing-app/resources/ratsnestcolors.xml /opt/fritzing-app/resources/properties.xml /opt/fritzing-app/resources/
COPY --from=fritzing-app /opt/fritzing-app/resources/templates /opt/fritzing-app/resources/templates
COPY --from=fritzing-app /opt/fritzing-app/resources/parts /opt/fritzing-app/resources/parts
# Fritzing's fonts (Droid Sans, OCR-A, ...), which part drawings name.
COPY --from=fritzing-app /opt/fritzing-app/resources/fonts /usr/share/fonts/fritzing
RUN fc-cache -f > /dev/null
COPY --from=vendor /opt/fritzing-render/vendor /opt/fritzing-render/vendor
COPY libraries/fritzing-parts-extra /opt/fritzing-render/libraries/fritzing-parts-extra
COPY --from=build /opt/fritzing-render/build/fritzing-render /usr/local/bin/fritzing-render
COPY --from=mcp /opt/fritzing-render/mcp /opt/fritzing-render/mcp
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
ENV FRITZING_RENDER_BIN=/usr/local/bin/fritzing-render \
    LANG=C.UTF-8 \
    QT_QPA_PLATFORM=offscreen \
    XDG_RUNTIME_DIR=/tmp/runtime \
    NODE_NO_WARNINGS=1
RUN mkdir -m 700 /tmp/runtime
WORKDIR /work
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

LABEL org.opencontainers.image.source="https://github.com/LeonFedotov/fritzing-render" \
      org.opencontainers.image.description="Render Fritzing breadboard diagrams (.fzz sketches or JSON) to SVG/PNG: CLI and MCP server, parts libraries included" \
      org.opencontainers.image.licenses="GPL-3.0-or-later"
