#!/bin/bash
#
# Synopsis: Execute commands in snowth build environment
#
# Usage: ./env.sh [CMD]
#
source $(dirname "$0")/${CI_ENV_SEL:-env.inc}
$*
