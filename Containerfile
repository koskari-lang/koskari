FROM python:3.14-alpine as builder

RUN apk add gcc musl-dev python3-dev
RUN pip install uv

WORKDIR /build
COPY setup.py setup.cfg pyproject.toml MANIFEST.in \
     LICENSE README.md koskari.pth koskari_importer.py \
     /build/
COPY koskari/ /build/koskari/

RUN uv build .


FROM python:3.14-alpine

ENTRYPOINT ["koskari"]
COPY --from=builder /build/dist /dist
RUN pip install --root-user-action=ignore --no-cache-dir --disable-pip-version-check \
        prompt_toolkit /dist/koskari-*.whl && \
    rm -rf /dist


# The end.
