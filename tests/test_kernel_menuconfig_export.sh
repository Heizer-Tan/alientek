#!/bin/sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
script="${repo_root}/scripts/kernel-menuconfig.sh"

grep -q 'bitbake virtual/kernel -c menuconfig' "${script}"
grep -q 'bitbake -c diffconfig virtual/kernel' "${script}"
grep -q 'TOPDIR=' "${script}"
grep -q '${topdir}/menuconfig/kernel.fragment.cfg' "${script}"
grep -q 'fragment.cfg' "${script}"
grep -q "read -r -d '' cmd <<'EOF'" "${script}"

echo "kernel-menuconfig.sh exports fragment after menuconfig"
