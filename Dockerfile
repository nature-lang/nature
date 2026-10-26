# docker build -t naturelang/nature:latest --build-arg VERSION=v0.1.0-beta .
FROM alpine:latest

WORKDIR /app
ARG VERSION

ENV VERSION=$VERSION

RUN sed -i 's#https\?://dl-cdn.alpinelinux.org/alpine#https://mirrors.tuna.tsinghua.edu.cn/alpine#g' /etc/apk/repositories && \
    apk update && apk upgrade && \
    rm -rf /var/cache/apk/*

COPY release/nax-${VERSION}-linux-amd64.tar.gz /tmp/nax.tar.gz

RUN tar -xzf /tmp/nax.tar.gz -C /usr/local && rm /tmp/nax.tar.gz

# 将 /usr/local/nax/bin 目录添加到 PATH 环境变量中
ENV PATH="/usr/local/nax/bin:${PATH}"
