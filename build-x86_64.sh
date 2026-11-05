#!/usr/bin/env bash
set -euo pipefail

# Parse optional arguments
CLEAN_BUILD=false

for arg in "$@"; do
  case $arg in
    --clean) CLEAN_BUILD=true ;;
  esac
done


# ==============================================================================
# CONFIGURATION & PATHS
# ==============================================================================
WINEVER="10.16"
WINE_SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${WINE_SRC_DIR}/build-x86_64"
INSTALL_PREFIX="/tmp/wine_build-x86_64-${WINEVER}"
OUTPUT_WCP="/mnt/d/winlator/wine-${WINEVER}-x86_64.wcp"
PREFIX_PACK="${WINE_SRC_DIR}/prefix-patterns/x86_64-container_pattern.tzst"

echo "================================================================="
echo "==> Starting Wine x86_64 Build Pipeline for Winlator..."
echo "================================================================="

unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR CC CROSSCC

if [ "${CLEAN_BUILD}" = true ]; then
  echo "==> [--clean passed] Cleaning build and install directories..."
  rm -rf "${BUILD_DIR}" "${INSTALL_PREFIX}"
fi

mkdir -p "${BUILD_DIR}" "${INSTALL_PREFIX}"
cd "${BUILD_DIR}"

# 1. Run ./configure ONLY if Makefile does not exist
if [ ! -f "Makefile" ]; then
  echo "==> Running Wine ./configure..."
  ../configure --prefix="${INSTALL_PREFIX}" \
    --enable-archs=i386,x86_64 \
    --enable-win64 \
    --with-mingw \
    --with-freetype \
    --with-fontconfig \
    --with-gstreamer \
    --with-vulkan \
    --with-opengl \
    --with-sdl \
    --with-gnutls \
    --with-xrandr \
    --with-xrender \
    --with-gssapi \
    --with-krb5 \
    --enable-tools \
    --disable-tests \
    --disable-win16 \
    --without-unwind \
    --without-dbus \
    --without-inotify \
    --without-netapi \
    --without-xshape \
    --without-xxf86vm \
    --without-xshm \
    --without-xcomposite \
    --without-xfixes \
    --without-xinerama \
    --without-capi \
    --without-coreaudio \
    --without-cups \
    --without-gphoto \
    --without-oss \
    --without-pcap \
    --without-pcsclite \
    --without-sane \
    --without-udev \
    --without-usb \
    --without-v4l2 \
    --without-wayland \
    --without-ffmpeg \
    --without-opencl \
    --without-gettext \
    --with-gettextpo=no
fi

# 2. Compile
NPROC=$(nproc)
echo "==> Compiling Wine using ${NPROC} threads..."
make -j"${NPROC}"

# 3. Install Binaries
echo "==> Installing binaries to ${INSTALL_PREFIX}..."
make install STRIP=true

# Userspace ntsync (no /dev/ntsync on Android); runs as x86_64 code under Box64
echo "==> Building libntsync_android.so for x86_64..."
NTSYNC_SRC="${HOME}/wine/ntsync-android"
cargo build --release --target x86_64-unknown-linux-gnu \
  --manifest-path "${NTSYNC_SRC}/Cargo.toml"
install -m 755 "${NTSYNC_SRC}/target/x86_64-unknown-linux-gnu/release/libntsync_android.so" \
  "${INSTALL_PREFIX}/lib/wine/x86_64-unix/"

# 4. Pruning & Stripping
echo "==> Pruning static libraries, def files, and manuals..."
find "${INSTALL_PREFIX}" -type f \( -name "*.a" -o -name "*.def" \) -delete
rm -rf "${INSTALL_PREFIX}/share/man" "${INSTALL_PREFIX}/share/doc"

echo "==> Stripping Linux host binaries..."
find "${INSTALL_PREFIX}" -name "*.so*" -exec strip --strip-unneeded {} + 2>/dev/null || true

echo "==> Stripping Windows MinGW PE binaries..."
find "${INSTALL_PREFIX}" -path "*x86_64-windows/*" \( -name "*.dll" -o -name "*.exe" \) \
  -exec x86_64-w64-mingw32-strip --strip-unneeded {} + 2>/dev/null || true
find "${INSTALL_PREFIX}" -path "*i386-windows/*" \( -name "*.dll" -o -name "*.exe" \) \
  -exec i686-w64-mingw32-strip --strip-unneeded {} + 2>/dev/null || true

# 5. Verification Steps
echo "==> Verifying PE architecture output..."
if [ -f "${INSTALL_PREFIX}/lib/wine/i386-windows/ntdll.dll" ] && [ -f "${INSTALL_PREFIX}/lib/wine/x86_64-windows/ntdll.dll" ] \
   && [ -f "${INSTALL_PREFIX}/lib/wine/x86_64-unix/libntsync_android.so" ]; then
  echo "SUCCESS: i386-windows, x86_64-windows and libntsync_android verified!"
else
  echo "ERROR: Missing required binaries."
  exit 1
fi

# 6. Inject Extra Package Assets
echo "==> Injecting profile.json and prefixPack.tzst..."
cat > "${INSTALL_PREFIX}/profile.json" <<EOF
{
    "type": "Wine",
    "versionName": "${WINEVER}-x86_64",
    "versionCode": 0,
    "description": "Wine ${WINEVER} x86_64 - Box64 build with userspace ntsync",
    "files": [],
    "wine": {
        "binPath": "bin",
        "libPath": "lib",
        "prefixPack": "prefixPack.tzst"
    }
}
EOF

cp -v "${PREFIX_PACK}" "${INSTALL_PREFIX}/prefixPack.tzst"

# 7. Compress Package into .wcp
echo "==> Packaging into Winlator Container Package (.wcp)..."
cd "${INSTALL_PREFIX}"
mkdir -p "$(dirname "${OUTPUT_WCP}")"
tar --exclude='include' --use-compress-program="zstd -T0 --ultra -16" -cf "${OUTPUT_WCP}" .

echo "================================================================="
echo "BUILD COMPLETE!"
echo "Package saved to: ${OUTPUT_WCP}"
echo "================================================================="
