pkgname=vicinae-patched
_pkgname=vicinae
pkgver=0.18.3
pkgrel=1
pkgdesc="Vicinae launcher with enhancements: multi-paste, paste as text, image preview, reveal in file explorer"
arch=('x86_64')
url="https://github.com/vicinaehq/vicinae"
license=('GPL3')

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
  "${pkgname}-${pkgver}.tar.gz::https://github.com/vicinaehq/${_pkgname}/archive/refs/tags/v${pkgver}.tar.gz"
  "vicinae-enhancements.patch"
)
sha256sums=(
  'SKIP'
  'SKIP'
)

prepare() {
  cd "${_pkgname}-${pkgver}"

  patch -Np1 -i "${srcdir}/vicinae-enhancements.patch"
}

build() {
  export CFLAGS="${CFLAGS} -march=znver4 -mtune=znver4 -O3 -pipe -fno-plt -fexceptions -Wp,-D_FORTIFY_SOURCE=3 -Wformat -Werror=format-security -fstack-clash-protection -fcf-protection -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"
  export CXXFLAGS="${CXXFLAGS} -march=znver4 -mtune=znver4 -O3 -pipe -fno-plt -fexceptions -Wp,-D_FORTIFY_SOURCE=3 -Wformat -Werror=format-security -fstack-clash-protection -fcf-protection -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"
  export LDFLAGS="${LDFLAGS} -Wl,-O1 -Wl,--sort-common -Wl,--as-needed -Wl,-z,relro -Wl,-z,now -Wl,-z,pack-relative-relocs"

  cmake -S "${_pkgname}-${pkgver}" -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_C_FLAGS_RELEASE="${CFLAGS}" \
    -DCMAKE_CXX_FLAGS_RELEASE="${CXXFLAGS}" \
    -DVICINAE_GIT_TAG="v${pkgver}" \
    -DVICINAE_GIT_COMMIT_HASH="arch" \
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
