#!/bin/bash
cd "$(dirname "$0")/../../"
sudo docker buildx build --load -t mooer-builder -f packaging/docker/Dockerfile .
