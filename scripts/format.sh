#!/usr/bin/env bash
# Run clang-format on all source files
find include src tests examples -name "*.hpp" -o -name "*.cpp" -o -name "*.h" \
  | xargs clang-format --style=file -i
echo "Format complete."
