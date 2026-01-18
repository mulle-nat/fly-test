FROM alpine:latest
RUN apk add --no-cache gcc musl-dev cmake make
WORKDIR /app
COPY main.c CMakeLists.txt ./
RUN cmake . && make
CMD ["./hello-server"]
