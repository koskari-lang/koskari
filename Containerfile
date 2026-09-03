FROM python:3.14-alpine AS builder

ENV PIP_NO_CACHE_DIR=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PIP_ROOT_USER_ACTION=ignore

# The evaluator threads dispatch through musttail, which needs clang
# (or GCC 15+); lld does the LTO link, and gcc supplies the runtime
# objects and libgcc that clang links against on musl.
RUN apk add clang lld gcc musl-dev python3-dev && \
    pip install uv

WORKDIR /build
COPY tools/regen_version.py /build/tools/
COPY setup.py setup.cfg pyproject.toml MANIFEST.in \
     LICENSE README.md koskari.pth ./
COPY koskari/ ./koskari/

ARG KOSKARI_COMMIT=
ARG KOSKARI_DIRTY=0
ARG KOSKARI_EXACT_TAG=
ENV KOSKARI_FORCE_VERSION_REGEN=1 \
    KOSKARI_COMMIT=${KOSKARI_COMMIT} \
    KOSKARI_DIRTY=${KOSKARI_DIRTY} \
    KOSKARI_EXACT_TAG=${KOSKARI_EXACT_TAG}

RUN mkdir .eggs && \
    uv build .


# ================================
# The actual runtime image


FROM python:3.14-alpine

ENV PIP_NO_CACHE_DIR=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PIP_ROOT_USER_ACTION=ignore

ARG KOSKARI_COMMIT=
ARG KOSKARI_VERSION=

LABEL org.opencontainers.image.title="koskari" \
      org.opencontainers.image.source="https://github.com/koskari-lang/koskari" \
      org.opencontainers.image.description="Koskari: A LISP dialect for Python" \
      org.opencontainers.image.licenses="GPL-3.0-or-later" \
      org.opencontainers.image.authors="Christopher O'Brien <obriencj@gmail.com>" \
      org.opencontainers.image.vendor="Koskari" \
      org.opencontainers.image.version="${KOSKARI_VERSION}" \
      org.opencontainers.image.revision="${KOSKARI_COMMIT}"

ENTRYPOINT ["koskari"]

# Raw runtime only: no extras. Consumers (the MCP images, the tools
# image, an interactive REPL image) layer their own dependencies on top.
COPY --from=builder /build/dist /dist
RUN pip install --find-links=/dist/ koskari && \
    rm -rf /dist


# The end.
