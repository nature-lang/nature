# docker build -t naturelang/nature:latest --build-arg VERSION=v0.1.0-beta .
FROM alpine:latest

WORKDIR /app
ARG VERSION

ENV VERSION=$VERSION

RUN sed -i 's#https\?://dl-cdn.alpinelinux.org/alpine#https://mirrors.tuna.tsinghua.edu.cn/alpine#g' /etc/apk/repositories && \
    apk update && apk upgrade && \
    rm -rf /var/cache/apk/*

COPY release/nature-${VERSION}-linux-amd64.tar.gz /tmp/nature.tar.gz

RUN tar -xzf /tmp/nature.tar.gz -C /usr/local && rm /tmp/nature.tar.gz

# 将 /usr/local/nature/bin 目录添加到 PATH 环境变量中
ENV PATH="/usr/local/nature/bin:${PATH}"
