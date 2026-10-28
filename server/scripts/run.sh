#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SERVER_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
IMAGE_NAME=spacchiufy-services:latest
CONTAINER_NAME=spacchiufy-services
ENV_FILE=${1:-"$SERVER_DIR/.env.docker"}
HOST_PORT=${2:-8081}

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is required to run the backend." >&2
  exit 1
fi

if [ ! -f "$ENV_FILE" ]; then
  echo "Environment file not found: $ENV_FILE" >&2
  echo "Copy $SERVER_DIR/.env.docker.example to $SERVER_DIR/.env.docker and edit it." >&2
  exit 1
fi

case "$HOST_PORT" in
  ''|*[!0-9]*)
    echo "Host port must be a number." >&2
    exit 1
    ;;
esac

if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
  echo "Image $IMAGE_NAME is missing. Build it first with $SCRIPT_DIR/build.sh." >&2
  exit 1
fi

mkdir -p "$SERVER_DIR/data" "$SERVER_DIR/storage"

if docker container inspect "$CONTAINER_NAME" >/dev/null 2>&1; then
  docker rm --force "$CONTAINER_NAME" >/dev/null
fi

docker run --detach \
  --name "$CONTAINER_NAME" \
  --restart unless-stopped \
  --env-file "$ENV_FILE" \
  --env HOST=0.0.0.0 \
  --env PORT=8081 \
  --env DB_PATH=/data/espspotify.db \
  --env STORAGE_PATH=/storage \
  --publish "$HOST_PORT:8081" \
  --volume "$SERVER_DIR/data:/data" \
  --volume "$SERVER_DIR/storage:/storage" \
  "$IMAGE_NAME"

printf 'Started %s as %s on host port %s.\n' "$IMAGE_NAME" "$CONTAINER_NAME" "$HOST_PORT"
