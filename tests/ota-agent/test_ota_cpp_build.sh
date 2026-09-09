#!/bin/sh
set -eu

recipe="meta-alientek/recipes-core/ota-agent/ota-agent_1.0.bb"
makefile="meta-alientek/recipes-core/ota-agent/files/src/Makefile"

grep -q 'file://src/ota-agent.cpp' "${recipe}"
grep -q 'file://src/ota-state.cpp' "${recipe}"
grep -q '^CXX ?=' "${makefile}"
grep -q '^CXXFLAGS ?=' "${makefile}"
grep -q 'ota-agent.cpp' "${makefile}"

echo "ota-agent c++ build wiring ready"
