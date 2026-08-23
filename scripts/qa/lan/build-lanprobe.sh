#!/usr/bin/env bash
# Build the LAN lobby probe harness (scripts/qa/lan/lanprobe.cpp).
#
# The harness links against the real Transport, UDP and IPEnumeration objects
# from an existing GeneralsXZH build, so it exercises the shipping networking
# code rather than a reimplementation of it. It reuses that build's own compile
# and link lines, which keeps it working when the build system changes.
#
# Usage: scripts/qa/lan/build-lanprobe.sh [build-dir]
#        default build-dir is build/linux64-deploy
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="${1:-${REPO_ROOT}/build/linux64-deploy}"
SRC="${REPO_ROOT}/scripts/qa/lan/lanprobe.cpp"
OUT="${BUILD_DIR}/lanprobe"

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
	echo "error: ${BUILD_DIR}/compile_commands.json not found." >&2
	echo "       Configure and build GeneralsXZH first:" >&2
	echo "         cmake --preset linux64-deploy" >&2
	echo "         cmake --build build/linux64-deploy --target GeneralsXZH" >&2
	exit 1
fi

# Reuse the compile line of a real GameNetwork translation unit so the harness
# sees exactly the defines, include paths and precompiled header the engine saw.
COMPILE_CMD="$(cd "${BUILD_DIR}" && python3 - "${SRC}" <<'PY'
import json, sys, re
src = sys.argv[1]
cc = json.load(open('compile_commands.json'))
for e in cc:
	if e['file'].endswith('GameNetwork/Transport.cpp'):
		cmd = e['command']
		cmd = re.sub(r' -o \S+\.o', ' -o lanprobe.o', cmd)
		cmd = re.sub(r' -c \S+Transport\.cpp', ' -c ' + src, cmd)
		print(cmd)
		break
else:
	sys.exit('error: Transport.cpp not found in compile_commands.json')
PY
)"

# Reuse the game's own link line, swapping the game's main TU for ours and
# keeping LinuxStubs.cpp.o, which defines the platform globals the archives need.
LINK_CMD="$(cd "${BUILD_DIR}" && ninja -t commands GeneralsMD/GeneralsXZH 2>/dev/null | tail -1 | python3 -c "
import re, sys
cmd = sys.stdin.read().strip()
cmd = cmd.split('&& ', 1)[1] if cmd.startswith(':') else cmd
cmd = cmd.rstrip(' &:')
cmd = re.sub(r'\S*z_generals\.dir/SDL3Main\.cpp\.o', 'lanprobe.o', cmd)
cmd = re.sub(r' -o \S*GeneralsXZH', ' -o lanprobe', cmd)
cmd = re.sub(r'-Wl,--dependency-file=\S+', '', cmd)
print(cmd)
")"

echo "==> compiling harness"
(cd "${BUILD_DIR}" && eval "${COMPILE_CMD}")
echo "==> linking harness"
(cd "${BUILD_DIR}" && eval "${LINK_CMD}")
echo "==> ${OUT}"
"${OUT}" enumerate
