EAPI=8

DESCRIPTION="Qt6 WebEngine application"
HOMEPAGE="https://github.com/Logawinner/Lindora"
SRC_URI="https://github.com/Logawinner/Lindora/archive/refs/tags/v1.1.tar.gz -> lindora-1.1.tar.gz"

LICENSE="MIT"
SLOT="0"
KEYWORDS="~amd64"

DEPEND="
    dev-qt/qtbase:6
    dev-qt/qtwebengine:6
    dev-qt/qtsvg:6
"

src_configure() {
    cmake -S . -B build
}

src_compile() {
    cmake --build build
}

src_install() {
    cmake --install build --prefix "${D}/usr"
}
