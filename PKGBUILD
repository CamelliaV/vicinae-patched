pkgname=vicinae-patched
_pkgname=vicinae
pkgver=0.18.0
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
  'd5803164ded217ecc445b430401c09879445b674cc52158d0a8bfb476ff85557'
  '444c2c1831075b41f4a358a83c9434ea7637e658a456da2f3f56c51f4b000b40'
)

prepare() {
  cd "${_pkgname}-${pkgver}"

  patch -Np1 -i "${srcdir}/vicinae-enhancements.patch"
}

build() {
  cmake -S "${_pkgname}-${pkgver}" -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
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
    -DLIBQALCULATE_BACKEND=ON

  cmake --build build
}

package() {
  DESTDIR="$pkgdir" cmake --install build
}
