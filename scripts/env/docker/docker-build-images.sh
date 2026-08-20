#!/usr/bin/env bash
# Build Docker images for GeneralsX development
# Usage: ./scripts/env/docker/docker-build-images.sh [linux|mingw|all]

set -e

BUILD_TARGET="${1:-all}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
DOCKERFILES_DIR="$PROJECT_ROOT/resources/dockerbuild"

# Image names
LINUX_IMAGE="generalsx/linux-builder:latest"
MINGW_IMAGE="generalsx/mingw-builder:latest"

echo "🐳 GeneralsX Docker Image Builder"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# GeneralsX @bugfix Claude 20/08/2026 Build the Linux image from Dockerfile.dev.
# Dockerfile.linux is gone. It was FROM ubuntu:26.04 (glibc 2.43), so every binary this
# script's image produced refused to start on anything older than Ubuntu 26.04 - while
# CI builds on ubuntu-24.04 and Dockerfile.dev already pinned ubuntu:24.04 to match.
# Two Linux build images that disagreed about the base is what let that happen, so there
# is now one. Dockerfile.dev is also godmode's agent base (agent.base_dockerfile).
#
# Two things differ from the old invocation and both are required:
#   - the build context is the repository ROOT, not resources/dockerbuild/, because
#     Dockerfile.dev COPYs vcpkg.json, vcpkg-lock.json and triplets/. The root
#     .dockerignore reduces that context to exactly those three entries.
#   - the image now bakes a pinned vcpkg at /opt/vcpkg-dist, so the first build no
#     longer clones vcpkg at an arbitrary commit. See Dockerfile.dev layer 3.
build_linux_image() {
    echo ""
    echo "📦 Building Linux native builder image..."
    echo "   Image: $LINUX_IMAGE"
    echo "   Dockerfile: $DOCKERFILES_DIR/Dockerfile.dev"
    echo "   Context: $PROJECT_ROOT (trimmed by .dockerignore)"
    echo "   Note: this bakes and prewarms vcpkg; expect 20+ minutes on a cold build."

    docker build \
        --platform linux/amd64 \
        -t "$LINUX_IMAGE" \
        -f "$DOCKERFILES_DIR/Dockerfile.dev" \
        "$PROJECT_ROOT"

    echo "✅ Linux builder image ready: $LINUX_IMAGE"
}

build_mingw_image() {
    echo ""
    echo "📦 Building MinGW cross-compiler image..."
    echo "   Image: $MINGW_IMAGE"
    echo "   Dockerfile: $DOCKERFILES_DIR/Dockerfile.mingw"
    
    docker build \
        --platform linux/amd64 \
        -t "$MINGW_IMAGE" \
        -f "$DOCKERFILES_DIR/Dockerfile.mingw" \
        "$DOCKERFILES_DIR"
    
    echo "✅ MinGW builder image ready: $MINGW_IMAGE"
}

show_summary() {
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "📋 Available Docker images:"
    docker images | grep -E "generalsx|REPOSITORY"
    echo ""
    echo "💡 Usage:"
    echo "   Linux builds:  Use image '$LINUX_IMAGE' (built from Dockerfile.dev)"
    echo "   MinGW builds:  Use image '$MINGW_IMAGE'"
    echo ""
    echo "🔧 Rebuild images:"
    echo "   ./scripts/env/docker/docker-build-images.sh all"
}

case "$BUILD_TARGET" in
    linux)
        build_linux_image
        ;;
    mingw)
        build_mingw_image
        ;;
    all)
        build_linux_image
        build_mingw_image
        ;;
    *)
        echo "❌ Invalid target: $BUILD_TARGET"
        echo "Usage: $0 [linux|mingw|all]"
        exit 1
        ;;
esac

show_summary
