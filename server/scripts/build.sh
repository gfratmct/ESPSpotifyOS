#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SERVER_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
IMAGE_NAME=spacchiufy-services:latest

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is required to build the backend." >&2
  exit 1
fi

docker build --tag "$IMAGE_NAME" "$SERVER_DIR"
printf 'Built %s.\n' "$IMAGE_NAME"
