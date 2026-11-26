FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake pkg-config libavformat-dev libavcodec-dev libavutil-dev nlohmann-json3-dev libcpp-httplib-dev libspdlog-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
COPY tests ./tests
COPY third_party/HCNetSDK ./third_party/HCNetSDK
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)" && ctest --test-dir build --output-on-failure
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends libavformat60 libavcodec60 libavutil58 libcpp-httplib0.14t64 libspdlog1.12-fmt9 curl ca-certificates tini && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 --no-create-home recording \
 && mkdir -p /opt/hk-recording-service/bin /opt/hk-recording-service/lib /data/logs /data/temp \
 && chown -R recording:recording /data /opt/hk-recording-service
COPY --from=build /src/build/hk-recording-service /opt/hk-recording-service/bin/
COPY --from=build /src/third_party/HCNetSDK/lib /opt/hk-recording-service/lib/
COPY web /opt/hk-recording-service/web
ENV LD_LIBRARY_PATH=/opt/hk-recording-service/lib:/opt/hk-recording-service/lib/HCNetSDKCom HKMEDIA_PORT=8181
WORKDIR /opt/hk-recording-service
USER recording
EXPOSE 8181
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s CMD curl -fsS "http://127.0.0.1:${HKMEDIA_PORT}/healthz" || exit 1
ENTRYPOINT ["/usr/bin/tini","--","/opt/hk-recording-service/bin/hk-recording-service"]
