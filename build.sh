#!/usr/bin/env bash
# Configure and build Inner Glow Effect on macOS (universal arm64 + x86_64).
#
# Requires: CMake 3.20+, Xcode command line tools (for clang and Rez),
# and AE_SDK_PATH pointing at the After Effects SDK root.
#
#   ./build.sh                  builds Release
#   ./build.sh Debug            builds Debug
#   ./build.sh Release install  builds and copies the .plugin into the AE plug-ins folder
#                               (may need sudo, depending on where AE is installed)

set -euo pipefail

cd "$(dirname "$0")"

CONFIG="${1:-Release}"

if [ -z "${AE_SDK_PATH:-}" ]; then
	cat <<'EOF'

AE_SDK_PATH is not set.

Download the After Effects SDK from https://developer.adobe.com/after-effects/
unpack it, then point AE_SDK_PATH at the folder that directly contains Examples/

  export AE_SDK_PATH="$HOME/AfterEffectsSDK"

Add that line to ~/.zshrc to make it stick, then run ./build.sh again.

EOF
	exit 1
fi

cmake -S . -B build -G "Xcode"
cmake --build build --config "$CONFIG"

if [ "${2:-}" = "install" ]; then
	cmake --install build --config "$CONFIG"
fi

echo
echo "Built: build/$CONFIG/InnerGlowEffect.plugin"
echo
