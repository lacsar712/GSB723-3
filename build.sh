#!/bin/sh
set -e
echo "=== Configuring mirror ==="
cat /etc/alpine-release
echo "https://mirrors.aliyun.com/alpine/latest-stable/main" > /etc/apk/repositories
echo "https://mirrors.aliyun.com/alpine/latest-stable/community" >> /etc/apk/repositories
echo "=== Installing build deps ==="
apk add --no-cache gcc musl-dev libc-dev
echo "=== Compiling ==="
gcc -O2 -Wall \
  backend/main.c \
  backend/routes.c \
  backend/database.c \
  backend/json_parser.c \
  backend/logger.c \
  -o server -static
echo "=== Build OK ==="
ls -la server
