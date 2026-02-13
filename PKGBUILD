pkgname=vicinae-patched
_pkgname=vicinae
pkgver=0.19.7
pkgrel=1
pkgdesc="Vicinae launcher with enhancements: multi-paste, paste as text, image preview, reveal in file explorer"
arch=('x86_64')
url="https://github.com/vicinaehq/vicinae"
license=('GPL3')
_upstream_commit=0c70267ab7e07d7972012fcf8ae58808a32a2e86
_upstream_short=0c70267a

depends=(
  'abseil-cpp'
  'cmark-gfm'
  'layer-shell-qt'
  'libqalculate'
  'libsecret'
  'libx11'
  'libxml2'
  'minizip'
  'openssl'
  'protobuf'
  'qt6-base'
  'qt6-svg'
  'qt6-wayland'
  'qtkeychain-qt6'
  'wayland'
)
makedepends=(
  'cmake'
  'glaze'
  'ninja'
  'nodejs'
  'npm'
  'patch'
)

provides=("vicinae=${pkgver}")
conflicts=('vicinae')

source=(
  "${pkgname}-${_upstream_short}.tar.gz::https://github.com/vicinaehq/${_pkgname}/archive/${_upstream_commit}.tar.gz"
  "vicinae-enhancements.patch"
)
sha256sums=(
  'SKIP'
  'SKIP'
)

_get_upstream_srcdir() {
  find "${srcdir}" -maxdepth 1 -type d -name "${_pkgname}-*" | head -n 1
}

prepare() {
  cd "$(_get_upstream_srcdir)"

  patch -Np1 -i "${srcdir}/vicinae-enhancements.patch"
}

build() {
  export CFLAGS="${CFLAGS} -march=znver4 -mtune=znver4 -O3 -pipe -fno-plt -fexceptions -Wp,-D_FORTIFY_SOURCE=3 -Wformat -Werror=format-security -fstack-clash-protection -fcf-protection -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"
  export CXXFLAGS="${CXXFLAGS} -march=znver4 -mtune=znver4 -O3 -pipe -fno-plt -fexceptions -Wp,-D_FORTIFY_SOURCE=3 -Wformat -Werror=format-security -fstack-clash-protection -fcf-protection -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"
  export LDFLAGS="${LDFLAGS} -Wl,-O1 -Wl,--sort-common -Wl,--as-needed -Wl,-z,relro -Wl,-z,now -Wl,-z,pack-relative-relocs"

  cmake -S "$(_get_upstream_srcdir)" -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_C_FLAGS_RELEASE="${CFLAGS}" \
    -DCMAKE_CXX_FLAGS_RELEASE="${CXXFLAGS}" \
    -DVICINAE_GIT_TAG="v0.19.7" \
    -DVICINAE_GIT_COMMIT_HASH="${_upstream_short}" \
    -DVICINAE_PROVENANCE="arch" \
    -DBUILD_TESTS=OFF \
    -DTYPESCRIPT_EXTENSIONS=ON \
    -DINSTALL_NODE_MODULES=ON \
    -DUSE_SYSTEM_PROTOBUF=ON \
    -DUSE_SYSTEM_ABSEIL=ON \
    -DUSE_SYSTEM_CMARK_GFM=ON \
    -DUSE_SYSTEM_LAYER_SHELL=ON \
    -DUSE_SYSTEM_QT_KEYCHAIN=ON \
    -DUSE_SYSTEM_GLAZE=ON \
    -DLIBQALCULATE_BACKEND=ON \
    -DLTO=ON

  cmake --build build
}

package() {
  DESTDIR="$pkgdir" cmake --install build
}
