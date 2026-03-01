#!/bin/bash

if [ -z "$1" ]; then
    echo "usage: ./release.sh 1.0.0"
    exit 1
fi

VERSION=$1

echo "building ohsh $VERSION..."
make build

if [ $? -ne 0 ]; then
    echo "build failed"
    exit 1
fi

echo "creating github release v$VERSION..."
gh release create "v$VERSION" ./ohsh \
    --title "ohsh v$VERSION" \
    --notes "ohsh v$VERSION" \
    --repo gabex47/ohsh

echo "done! release v$VERSION is live"