#!/bin/sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
recipe="${repo_root}/meta-alientek/recipes-kernel/modules/ap3216c/ap3216c-module_1.0.bb"

if ! grep -Fqx 'S = "${WORKDIR}/src"' "${recipe}"; then
    echo 'expected ap3216c-module recipe to read sources from ${WORKDIR}/src' >&2
    exit 1
fi

if ! grep -Fqx 'B = "${S}"' "${recipe}"; then
    echo 'expected ap3216c-module recipe to build in ${S}' >&2
    exit 1
fi

for f in ap3216c.h ap3216c-i2c.c ap3216c-hw.c ap3216c-misc.c Makefile; do
    if ! grep -Fq "file://${f};subdir=src" "${recipe}"; then
        echo "expected ${f} to unpack into src subdir" >&2
        exit 1
    fi
done

echo 'ap3216c-module recipe binds S/B to WORKDIR/src'
