#!/usr/bin/env bash
# GeneralsX @bugfix Claude 20/08/2026 Report the glibc floor of a deployed Linux tree.
#
# Why this exists: a build and a deploy can both report success and still leave a binary
# that will not start, because glibc symbol versioning is forward-only. A binary that
# references GLIBC_2.43 runs on glibc >= 2.43 and nowhere else, and the only symptom is
#
#   .../GeneralsX: /lib/x86_64-linux-gnu/libm.so.6: version `GLIBC_2.43' not found
#
# at exec time. That is what happened when the Docker builder image was FROM ubuntu:26.04
# while the machine running the game was not. This turns it into a message at deploy time.
#
# Usage: check-glibc-baseline.sh <binary> [extra .so files...]
#        check-glibc-baseline.sh --dir <directory>   # binary + every .so beside it
#
# Advisory only: exits 0 even when the floor is above the running host's glibc, because
# cross-building for a newer target on purpose is legitimate. It never exits non-zero for a
# missing objdump either - binutils is not a runtime dependency of the game.

set -uo pipefail

floor_of() {
    objdump -T "$1" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sed 's/^GLIBC_//' | sort -uV | tail -1
}

# Returns 0 when $1 <= $2 under version ordering.
version_le() {
    [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$2" ]
}

if ! command -v objdump >/dev/null 2>&1; then
    echo "   glibc floor: unknown (objdump not installed; apt install binutils)"
    exit 0
fi

TARGETS=()
if [[ "${1:-}" == "--dir" ]]; then
    DIR="${2:?--dir needs a directory}"
    while IFS= read -r f; do
        TARGETS+=("$f")
    done < <(find "$DIR" -maxdepth 1 -type f \( -perm -u+x -o -name '*.so' -o -name '*.so.*' \) | sort)
else
    TARGETS=("$@")
fi

OVERALL="0"
WORST=""
for t in ${TARGETS[@]+"${TARGETS[@]}"}; do
    [ -f "$t" ] || continue
    f="$(floor_of "$t")"
    [ -n "$f" ] || continue
    if ! version_le "$f" "$OVERALL"; then
        OVERALL="$f"
        WORST="$t"
    fi
done

if [ "$OVERALL" = "0" ]; then
    echo "   glibc floor: none required"
    exit 0
fi

HOST="$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}')"
echo "   glibc floor: ${OVERALL} (highest requirement: $(basename "$WORST"))"

if [ -n "$HOST" ]; then
    if version_le "$OVERALL" "$HOST"; then
        echo "   This host has glibc ${HOST} - OK."
    else
        echo ""
        echo "WARNING: this host has glibc ${HOST}, the binaries need >= ${OVERALL}."
        echo "         They will fail at startup with: version \`GLIBC_${OVERALL}' not found"
        echo "         The build image is newer than this machine. Rebuild the builder image:"
        echo "             ./scripts/env/docker/docker-build-images.sh linux"
        echo "         and check the FROM line in resources/dockerbuild/Dockerfile.dev."
    fi
fi

echo "   (Anyone on a distribution older than glibc ${OVERALL} needs the Flatpak instead;"
echo "    it carries its own glibc and is what the GitHub releases ship.)"
