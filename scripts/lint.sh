#!/usr/bin/env bash
# Run clang-tidy via compile_commands.json (requires debug build first)
BUILD_DIR="${1:-build/debug}"
find src include -name "*.cpp" \
  | xargs clang-tidy -p "$BUILD_DIR" --config-file=.clang-tidy
