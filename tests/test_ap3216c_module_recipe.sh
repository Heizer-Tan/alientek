#!/bin/sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
recipe="${repo_root}/meta-alientek/recipes-kernel/ap3216c/ap3216c-module_1.0.bb"

if ! grep -Fqx 'S = "${WORKDIR}/src"' "${recipe}"; then
    echo 'expected ap3216c-module recipe to read sources from ${WORKDIR}/src' >&2
    exit 1
fi

if ! grep -Fqx 'B = "${S}"' "${recipe}"; then
    echo 'expected ap3216c-module recipe to build in ${S}' >&2
    exit 1
fi

if ! grep -Fq 'file://ap3216c.c;subdir=src' "${recipe}"; then
    echo 'expected ap3216c.c to unpack into src subdir' >&2
    exit 1
fi

if ! grep -Fq 'file://Makefile;subdir=src' "${recipe}"; then
    echo 'expected Makefile to unpack into src subdir' >&2
    exit 1
fi

echo 'ap3216c-module recipe binds S/B to WORKDIR/src'
