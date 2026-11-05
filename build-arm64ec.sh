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
TOOLS_DIR="${WINE_SRC_DIR}/build-tools"
BUILD_DIR="${WINE_SRC_DIR}/build-arm64ec"
INSTALL_PREFIX="/tmp/wine_build-arm64ec"
OUTPUT_WCP="/mnt/d/winlator/wine-${WINEVER}-arm64ec.wcp"
PREFIX_PACK="${WINE_SRC_DIR}/prefix-patterns/arm64ec-container_pattern.tzst"
LLVM_MINGW="${HOME}/toolchains/llvm-mingw"

# Winlator runs the arm64 unix side directly, so it must use the rootfs loader and libraries
ROOTFS="/data/data/com.winlator/files/rootfs"

echo "================================================================="
echo "==> Starting Wine ARM64EC Build Pipeline for Winlator..."
echo "================================================================="

[ -x "${LLVM_MINGW}/bin/arm64ec-w64-mingw32-clang" ] || { echo "ERROR: llvm-mingw not found in ${LLVM_MINGW}"; exit 1; }
export PATH="${LLVM_MINGW}/bin:${PATH}"
unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR CC CROSSCC

if [ "${CLEAN_BUILD}" = true ]; then
  echo "==> [--clean passed] Cleaning build and install directories..."
  rm -rf "${TOOLS_DIR}" "${BUILD_DIR}" "${INSTALL_PREFIX}"
fi

NPROC=$(nproc)

# 1. Host tools (widl, winebuild, wrc, nls) built natively for x86_64
mkdir -p "${TOOLS_DIR}"
cd "${TOOLS_DIR}"
if [ ! -f "Makefile" ]; then
  echo "==> Configuring host tools..."
  # sfnt2fon needs FreeType to build the bitmap fonts
  ../configure --enable-win64 --without-x --without-gstreamer \
    --without-vulkan --without-opengl --without-unwind --disable-tests
fi
echo "==> Building host tools..."
make __tooldeps__ -j"${NPROC}"
make -C nls -j"${NPROC}"

# 2. Cross build for aarch64 Linux with arm64ec/aarch64/i386 PE modules
mkdir -p "${BUILD_DIR}" "${INSTALL_PREFIX}"
cd "${BUILD_DIR}"

# libgstreamer-plugins-base1.0-dev can't be installed for amd64 and arm64 at
# once, so the arm64 .pc files and .so links live in a private folder
ARM64_EXTRA="${HOME}/toolchains/arm64-extra"
[ -f "${ARM64_EXTRA}/pkgconfig/gstreamer-video-1.0.pc" ] || { echo "ERROR: ${ARM64_EXTRA} is missing"; exit 1; }
if [ -f "Makefile" ] && ! grep -q "${ARM64_EXTRA}" Makefile; then
  echo "==> Reconfiguring to use ${ARM64_EXTRA}..."
  rm -f Makefile
fi

if [ ! -f "Makefile" ]; then
  echo "==> Running Wine ARM64EC ./configure..."
  PKG_CONFIG_LIBDIR="${ARM64_EXTRA}/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig" \
  ../configure --prefix="${INSTALL_PREFIX}" \
    --host=aarch64-linux-gnu \
    --enable-archs=arm64ec,aarch64,i386 \
    --with-mingw=clang \
    --with-wine-tools="${TOOLS_DIR}" \
    --with-freetype \
    --with-fontconfig \
    --with-gstreamer \
    --with-vulkan \
    --with-opengl \
    --with-gnutls \
    --with-xrandr \
    --with-xrender \
    --with-pulse \
    --with-alsa \
    --enable-tools \
    --disable-tests \
    --disable-win16 \
    --without-unwind \
    --without-dbus \
    --without-inotify \
    --without-netapi \
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
    --without-sdl \
    --without-krb5 \
    --without-gssapi \
    --without-gettext \
    --with-gettextpo=no \
    CC=aarch64-linux-gnu-gcc \
    CFLAGS="-O2 -pipe -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types" \
    CROSSCFLAGS="-O2 -pipe" \
    LDFLAGS="-Wl,--dynamic-linker=${ROOTFS}/lib/ld-linux-aarch64.so.1 -Wl,-rpath=${ROOTFS}/usr/lib -Wl,--sort-common,--as-needed"
fi

# 3. Compile
echo "==> Compiling Wine using ${NPROC} threads..."
make -j"${NPROC}"

# 4. Install Binaries
echo "==> Installing binaries to ${INSTALL_PREFIX}..."
make install STRIP=true

# Userspace ntsync for Android (no /dev/ntsync); wineserver and ntdll dlopen it
echo "==> Building libntsync_android.so..."
NTSYNC_SRC="${HOME}/wine/ntsync-android"
CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc \
  "${HOME}/.cargo/bin/cargo" build --release --target aarch64-unknown-linux-gnu \
  --manifest-path "${NTSYNC_SRC}/Cargo.toml"
install -m 755 "${NTSYNC_SRC}/target/aarch64-unknown-linux-gnu/release/libntsync_android.so" \
  "${INSTALL_PREFIX}/lib/wine/aarch64-unix/"

# 5. Pruning & Stripping
echo "==> Pruning static libraries, def files, and manuals..."
find "${INSTALL_PREFIX}" -type f \( -name "*.a" -o -name "*.def" \) -delete
rm -rf "${INSTALL_PREFIX}/share/man" "${INSTALL_PREFIX}/share/doc"

echo "==> Stripping Linux host binaries..."
find "${INSTALL_PREFIX}" -name "*.so*" -exec aarch64-linux-gnu-strip --strip-unneeded {} + 2>/dev/null || true
find "${INSTALL_PREFIX}/bin" -type f -exec aarch64-linux-gnu-strip --strip-unneeded {} + 2>/dev/null || true

echo "==> Stripping Windows PE binaries..."
find "${INSTALL_PREFIX}" \( -name "*.dll" -o -name "*.exe" -o -name "*.sys" -o -name "*.drv" \) \
  -path "*-windows/*" -exec llvm-strip --strip-unneeded {} + 2>/dev/null || true

# 6. Verification Steps
echo "==> Verifying PE architecture output..."
if [ -f "${INSTALL_PREFIX}/lib/wine/aarch64-windows/ntdll.dll" ] && [ -f "${INSTALL_PREFIX}/lib/wine/i386-windows/ntdll.dll" ] \
   && [ -f "${INSTALL_PREFIX}/lib/wine/aarch64-unix/ntdll.so" ]; then
  echo "SUCCESS: aarch64-windows, i386-windows and aarch64-unix modules verified!"
else
  echo "ERROR: Missing required ntdll binaries."
  exit 1
fi

# 7. Inject Extra Package Assets
echo "==> Injecting profile.json and prefixPack.tzst..."
cat > "${INSTALL_PREFIX}/profile.json" <<EOF
{
    "type": "Wine",
    "versionName": "${WINEVER}-arm64ec",
    "versionCode": 0,
    "description": "Wine ${WINEVER} ARM64EC - native ARM64 Wine with FEX/wowbox64 x86 emulation",
    "files": [],
    "wine": {
        "binPath": "bin",
        "libPath": "lib",
        "prefixPack": "prefixPack.tzst"
    }
}
EOF

cp -v "${PREFIX_PACK}" "${INSTALL_PREFIX}/prefixPack.tzst"

# 8. Compress Package into .wcp
echo "==> Packaging into Winlator Container Package (.wcp)..."
cd "${INSTALL_PREFIX}"
mkdir -p "$(dirname "${OUTPUT_WCP}")"
tar --exclude='include' --use-compress-program="zstd -T0 --ultra -16" -cf "${OUTPUT_WCP}" .

echo "================================================================="
echo "BUILD COMPLETE!"
echo "Package saved to: ${OUTPUT_WCP}"
echo "================================================================="
