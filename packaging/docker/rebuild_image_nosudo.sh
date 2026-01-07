#!/bin/bash
cd "$(dirname "$0")/../../"
docker buildx build --load -t mooer-builder -f packaging/docker/Dockerfile .
