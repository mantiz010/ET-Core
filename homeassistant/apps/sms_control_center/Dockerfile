FROM python:3.13.7-alpine3.22
ARG BUILD_VERSION
ARG BUILD_ARCH
LABEL io.hass.version="${BUILD_VERSION}" io.hass.type="app" io.hass.arch="${BUILD_ARCH}"
RUN pip install --no-cache-dir aiohttp==3.12.15
WORKDIR /app
COPY server.py store.py ./
COPY static ./static
ENV PYTHONUNBUFFERED=1
CMD ["python", "server.py"]
