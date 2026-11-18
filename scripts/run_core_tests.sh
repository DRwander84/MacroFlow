#!/usr/bin/env sh
# Compiles and runs the pure-C++ tests with plain g++ (Linux/macOS/MSYS2), no Qt needed.
set -e
cd "$(dirname "$0")/.."
mkdir -p /tmp/mmr_tests
for t in EventSerializationTests MacroTimingTests ShortcutTests MarkerBuilderTests; do
  g++ -std=c++20 -Wall -Wextra -O1 -I. tests/$t.cpp core/MacroTypes.cpp core/EventBuffer.cpp core/RecordingEngine.cpp \
      core/PlaybackEngine.cpp core/ShortcutTypes.cpp core/MarkerBuilder.cpp -pthread -o /tmp/mmr_tests/$t
  /tmp/mmr_tests/$t
done
