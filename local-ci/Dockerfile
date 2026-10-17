# Local replica of the GitHub runners used by the Tiny Tapeout workflows.
# Built by local-ci/run from a staged context that contains this file plus the
# requirement files of the current tt-support-tools checkout, so the image is
# rebuilt automatically whenever those change.

# --- KLayout + Magic for precheck, from the same Nix pins as CI ---------------
FROM nixos/nix:2.28.3 AS precheck-tools
COPY precheck-default.nix /src/default.nix
RUN nix-build --option sandbox false --option filter-syscalls false \
        -E '(import /src/default.nix {}).buildInputs' -o /out/tool \
 && mkdir -p /closure /tools/bin \
 && cp -a $(nix-store -qR /out/tool*) /closure/ \
 && for d in /out/tool*; do \
        for b in "$(readlink -f "$d")"/bin/*; do ln -s "$b" /tools/bin/; done; \
    done

FROM docker:29.1.3-cli AS docker-cli

# --- Runner image -------------------------------------------------------------
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git jq make xz-utils \
        python3 python3-venv python3-dev build-essential \
        libcairo2 librsvg2-bin pngquant pandoc fonts-dejavu-core \
 && rm -rf /var/lib/apt/lists/*

# Same iverilog build the gl_test action installs
RUN curl -fsSL -o /tmp/iverilog.deb \
        https://github.com/TinyTapeout/iverilog/releases/download/v13.0/iverilog_13.0-1_amd64.deb \
 && apt-get update && apt-get install -y /tmp/iverilog.deb \
 && rm -rf /tmp/iverilog.deb /var/lib/apt/lists/*

# Same Typst version the docs action installs
RUN curl -fsSL https://github.com/typst/typst/releases/download/v0.13.1/typst-x86_64-unknown-linux-musl.tar.xz \
        | tar -xJ -C /usr/local/bin --strip-components=1 typst-x86_64-unknown-linux-musl/typst

# Docker CLI, so LibreLane can start its own container on the host daemon
COPY --from=docker-cli /usr/local/bin/docker /usr/local/bin/docker

COPY --from=precheck-tools /closure /nix/store
COPY --from=precheck-tools /tools/bin /opt/precheck-tools/bin

# One venv per CI job type, because their pinned requirements conflict
# (e.g. klayout 0.29 for tt-support-tools vs 0.30 for precheck).
ARG LIBRELANE_VERSION=3.0.14
COPY tt-requirements.txt precheck-requirements.txt test-requirements.txt /req/
RUN python3 -m venv /opt/venv-tt \
 && /opt/venv-tt/bin/pip install --no-cache-dir -r /req/tt-requirements.txt \
 && /opt/venv-tt/bin/pip install --no-cache-dir librelane==${LIBRELANE_VERSION}
RUN python3 -m venv /opt/venv-test \
 && /opt/venv-test/bin/pip install --no-cache-dir -r /req/test-requirements.txt
RUN python3 -m venv /opt/venv-precheck \
 && /opt/venv-precheck/bin/pip install --no-cache-dir -r /req/precheck-requirements.txt
