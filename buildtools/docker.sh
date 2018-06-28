#!/bin/bash

HELP='# Dockerized Standard Actions

This script provides means to execute standard actions inside the controlled environment of a docker
container. Examples:

* `docker.sh --create` -- create docker build container

* `docker.sh --action <[os/]action>` -- execute standard action defined by ./buildtools/<[os/]action>.sh

* `docker.sh --exec <CMD>` -- execute command inside container and exits

* `docker.sh --shell` -- drop to an interactive shell

DISCLAIMER: This script is not intended to allow for more complex docker interactions.
If the provided commands are not working for you, on the spot you should work with
docker directly, instead of changing/extending this script. 

Hint: Running this script as `bash -x ./docker.sh` will print the precise docker commands
that were issued to stdout.

## Docker Images

The following docker images are created:

- The `reconnoiter-el7-base` image contains centos:7 and all build dependencies
- The `reconnoiter-el7-build` container contains a copy of the work tree at
  build time, and build artifacts.
- The `reconnoiter-el7` is based on the reconnoiter-build container, and debugging
  tools and can be used to insert patches that does not require a re-build.

'

cd $(dirname $0); cd ..

DOCKER_RUN="sudo docker run --ulimit core=-1"

set -e

while [[ "$#" > 0 ]]
do
    cmd="$1"
    shift
    case $cmd in
        --help)
            printf '%s' "$HELP";
            exit 0
            ;;
        --set-core-pattern)
            docker run --rm --ulimit core=-1 --privileged centos:7 bash -c 'echo "core[%t].%e.%p" > /proc/sys/kernel/core_pattern'
            ;;
        #
        # Create Docker Images
        #
        # - The reconnoiter-el7-base image contains centos:7 and all build dependencies
        # - The reconnoiter-el7-build container contains a copy of the work tree at
        #   build time, and build artifacts.
        # - The reconnoiter-el7 is based on the reconnoiter-build container, and debugging
        #   tools and can be used to insert patches that don't require a re-build.
        #
        --create)
            echo "Rebuilding reconnoiter-el7-* images"
            sudo docker build --no-cache . -f buildtools/el7/Dockerfile-base -t reconnoiter-el7-base
            sudo docker build --no-cache . -f buildtools/el7/Dockerfile-build -t reconnoiter-el7-build
            sudo docker build --no-cache . -f buildtools/el7/Dockerfile-run -t reconnoiter-el7
            ;;
        --create-build)
            echo "Rebuilding reconnoiter-el7-build/-run images"
            sudo docker build --no-cache . -f buildtools/el7/Dockerfile-build -t reconnoiter-el7-build
            sudo docker build --no-cache . -f buildtools/el7/Dockerfile-run -t reconnoiter-el7
            ;;
        --create-run)
            echo "Rebuilding reconnoiter-el7-run images"
            sudo docker build --no-cache . -f buildtools/el7/Dockerfile-run -t reconnoiter-el7
            ;;
        --push)
            registry=$1
            shift
            echo "Pushing reconnoiter-el7 to registry $registry"
            sudo docker tag reconnoiter-el7 "$registry/reconnoiter-el7"
            sudo docker push "$registry/reconnoiter-el7"
            ;;
        #
        # Running reconnoiter-el7
        #
        --action)
            ACTION=$1
            ARG=$2
            shift
            $DOCKER_RUN reconnoiter-el7-base /reconnoiter-base/cmd.sh "/reconnoiter/buildtools/${ACTION}.sh ${ARG}"
            exit 0
            ;;
        --exec)
            $DOCKER_RUN -p 8112:8112 reconnoiter-el7 $@
            exit 0
            ;;
        --shell)
            $DOCKER_RUN -it -p 8112:8112 reconnoiter-el7 bash
            exit 0
            ;;
        #
        # Running reconnoiter-el7-base with mounted working directory.
        #
        # This can be used to build and run reconnoiter within an el7 environment, while keeping all
        # files in place.
        #
        --mnt-action)
            ACTION=$1
            ARG=$2
            shift
            $DOCKER_RUN -v $(pwd):/reconnoiter reconnoiter-el7-base /reconnoiter-base/cmd.sh "./buildtools/${ACTION}.sh ${ARG}"
            exit 0
            ;;
        --mnt-exec)
            echo "Running reconnoiter-el7-base container image"
            $DOCKER_RUN -v $(pwd):/reconnoiter reconnoiter-el7-base $@
            exit 0
            ;;
        --mnt-shell)
            echo "Running reconnoiter-el7-base container image"
            $DOCKER_RUN -it --privileged -p 43191:43191 -v $(pwd):/reconnoiter reconnoiter-el7-base $@
            exit 0
            ;;
        *)
            echo "Unknown command $1"
            exit 1
            ;;
    esac;
done

exit 0
