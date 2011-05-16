#!/bin/sh

set -e

ln -sf pkg/debian debian

dpkg-buildpackage -rfakeroot -tc

rm debian
