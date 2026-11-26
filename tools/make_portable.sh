#!/usr/bin/env bash
# Builds a self-contained portable bundle: dist/voxfx-<version>-linux-x86_64.tar.gz
#
#   voxfx-<version>/
#     voxfx            launcher (run this)
#     install.sh       adds voxfx to the app menu (~/.local, no root needed)
#     uninstall.sh
#     START-HERE.txt
#     bin/  lib/  plugins/  share/
#
# Bundled: Qt 6 (+ Wayland, X11, TLS, portal plugins), ICU, libsndfile and codecs,
# soxr, libebur128, libevdev, libproxy. Taken from the user's system (must match
# it): glibc/libstdc++, graphics drivers, X11/Wayland/xkb, fonts, D-Bus, systemd,
# GLib, PipeWire, ALSA, OpenSSL/GnuTLS/Kerberos and other base libraries.
#
# Uses only standard tools (cmake, ldd, cp, tar). Build on the oldest distro you
# want to support; the script prints the minimum glibc at the end.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(sed -n 's/^project(voxfx VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
BUILD="$ROOT/build-portable"
STAGE="$BUILD/stage"
NAME="voxfx-$VERSION"
OUT="$ROOT/dist/$NAME"
QT_PLUGINS=${QT_PLUGINS:-$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/qt6/plugins)}

echo "==> building"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DVOXFX_BUILD_TESTS=OFF \
      -DVOXFX_PORTABLE=ON -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
cmake --build "$BUILD" --target voxfx
rm -rf "$STAGE" "$OUT"
DESTDIR="$STAGE" cmake --install "$BUILD" >/dev/null

echo "==> laying out $OUT"
mkdir -p "$OUT/bin" "$OUT/lib" "$OUT/plugins"
cp "$STAGE/usr/bin/voxfx" "$OUT/bin/"
strip --strip-unneeded "$OUT/bin/voxfx"
cp -r "$STAGE/usr/share" "$OUT/share"
cp "$ROOT/packaging/portable/voxfx" "$ROOT/packaging/portable/install.sh" \
   "$ROOT/packaging/portable/uninstall.sh" "$ROOT/packaging/portable/START-HERE.txt" "$OUT/"
chmod +x "$OUT/voxfx" "$OUT/install.sh" "$OUT/uninstall.sh"
sed -i "s/@VERSION@/$VERSION/g" "$OUT/START-HERE.txt"
printf '[Paths]\nPrefix = ..\nPlugins = plugins\nLibraries = lib\n' > "$OUT/bin/qt.conf"

# Qt plugins the app needs.
for dir in platforms wayland-shell-integration wayland-decoration-client \
           wayland-graphics-integration-client xcbglintegrations platformthemes \
           platforminputcontexts imageformats tls networkinformation iconengines; do
    [ -d "$QT_PLUGINS/$dir" ] || continue
    mkdir -p "$OUT/plugins/$dir"
    cp "$QT_PLUGINS/$dir"/*.so "$OUT/plugins/$dir/" 2>/dev/null || true
done
# Drop plugins we don't want (pull in unrelated stacks or are for embedded use).
rm -f "$OUT"/plugins/platforms/lib{qeglfs,qlinuxfb,qminimalegl,qvkkhrdisplay,qvnc}.so \
      "$OUT"/plugins/platformthemes/libqgtk3.so

# Libraries provided by the user's system (regex on the soname).
SYSTEM_LIBS='^(ld-linux|libc|libm|libdl|librt|libpthread|libresolv|libutil|libanl|libstdc\+\+|libgcc_s|'\
'libGL|libEGL|libGLX|libGLdispatch|libOpenGL|libgbm|libdrm|libvulkan|'\
'libX|libxcb|libxkbcommon|libwayland|libICE|libSM|'\
'libfontconfig|libfreetype|libharfbuzz|libgraphite2|libpng16|libbrotli|'\
'libz\.|libzstd|liblzma|libbz2|liblz4|libexpat|libffi|libuuid|libbsd|libmd\.|'\
'libdbus-1|libsystemd|libudev|libcap|libgcrypt|libgpg-error|libselinux|libmount|libblkid|'\
'libglib-2|libgobject-2|libgio-2|libgmodule-2|libgthread-2|libpcre2-8|'\
'libpipewire|libspa|libasound|libjack|libpulse|'\
'libssl|libcrypto|libgnutls|libnettle|libhogweed|libgmp|libtasn1|libp11-kit|libidn2|libunistring|'\
'libkrb5|libk5crypto|libcom_err|libkeyutils|libgssapi_krb5|libnghttp2|libpsl|libssh\.|libldap|liblber|libsasl2)'

# Copy every non-system dependency of the binary, the plugins and (recursively)
# of what we copy.
copy_deps() {
    ldd "$1" 2>/dev/null | awk '/=> \// {print $1, $3}' | while read -r soname path; do
        if echo "$soname" | grep -Eq "$SYSTEM_LIBS"; then continue; fi
        if [ ! -e "$OUT/lib/$soname" ]; then
            cp -L "$path" "$OUT/lib/$soname"
            copy_deps "$OUT/lib/$soname"
        fi
    done
}
copy_deps "$OUT/bin/voxfx"
find "$OUT/plugins" -name '*.so' | while read -r p; do copy_deps "$p"; done
# libproxy loads its backend from a private dir next to itself.
if [ -e "$OUT/lib/libproxy.so.1" ] && [ -d /usr/lib/x86_64-linux-gnu/libproxy ]; then
    mkdir -p "$OUT/lib/libproxy"
    cp -L /usr/lib/x86_64-linux-gnu/libproxy/*.so "$OUT/lib/libproxy/"
    for b in "$OUT"/lib/libproxy/*.so; do copy_deps "$b"; done
fi
find "$OUT/lib" "$OUT/plugins" -name '*.so*' -exec strip --strip-unneeded {} + 2>/dev/null || true

echo "==> checking"
missing=$(LD_LIBRARY_PATH= ldd "$OUT/bin/voxfx" | grep "not found" || true)
[ -z "$missing" ] || { echo "unresolved libraries:"; echo "$missing"; exit 1; }
qtcore=$(realpath "$(ldd "$OUT/bin/voxfx" | awk '/libQt6Core/ {print $3}')")
case "$qtcore" in "$OUT"/lib/*) ;; *) echo "Qt is not loaded from the bundle ($qtcore)"; exit 1 ;; esac
GLIBC=$( (find "$OUT/bin/voxfx" "$OUT/lib" "$OUT/plugins" -type f -name 'voxfx' -o -type f -name '*.so*' \
          | xargs objdump -T 2>/dev/null || true) | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1)

mkdir -p "$ROOT/dist"
tar -C "$ROOT/dist" -czf "$ROOT/dist/$NAME-linux-x86_64.tar.gz" "$NAME"
echo "==> $ROOT/dist/$NAME-linux-x86_64.tar.gz ($(du -h "$ROOT/dist/$NAME-linux-x86_64.tar.gz" | cut -f1)," \
     "$(ls "$OUT/lib" | wc -l) bundled libraries, needs $GLIBC or newer)"
