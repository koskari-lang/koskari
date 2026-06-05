FROM python:3.14-alpine as builder

RUN apk add gcc musl-dev python3-dev
RUN pip install --root-user-action=ignore --no-cache-dir --disable-pip-version-check \
    uv

WORKDIR /build
COPY setup.py setup.cfg pyproject.toml MANIFEST.in \
     LICENSE README.md koskari.pth \
     /build/
COPY koskari/ /build/koskari/
COPY tools/regen_version.py /build/tools/

RUN uv build .


FROM python:3.14-alpine

ENTRYPOINT ["koskari"]
COPY --from=builder /build/dist /dist
RUN pip install --root-user-action=ignore --no-cache-dir --disable-pip-version-check \
        --find-links=/dist/ koskari[repl,lsp,pygments,sphinx] && \
    rm -rf /dist


# The end.
