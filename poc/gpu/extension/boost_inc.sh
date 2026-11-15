#!/bin/bash
# Prints -I flags for the vendored Boost (src/third_party/boost plus libs/*/include), header-only use.
R="$(cd "$(dirname "$0")/../../.." && pwd)"
printf -- '-I%s ' "$R/src/third_party/boost"
for d in "$R"/src/third_party/boost/libs/*/include; do printf -- '-I%s ' "$d"; done
