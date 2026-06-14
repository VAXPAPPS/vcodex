#!/usr/bin/env bash
# build-deb.sh - A script to package the vcodex application into a .deb package.
set -e

# Define colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Starting vcodex packaging process ===${NC}"

# Check for required tools
for tool in meson ninja dpkg-deb; do
    if ! command -v "$tool" &> /dev/null; then
        echo -e "${RED}Error: '$tool' is not installed. Please install it first.${NC}"
        exit 1
    fi
done

# Step 1: Detect architecture
ARCH=$(dpkg --print-architecture 2>/dev/null || echo "amd64")
VERSION="0.1.0"
PKG_DIR="build/deb_dist"

echo -e "${BLUE}Target architecture:${NC} $ARCH"
echo -e "${BLUE}Package version:${NC} $VERSION"

# Step 2: Configure and compile the project with prefix=/usr
echo -e "${BLUE}Configuring build with prefix=/usr...${NC}"
if [ -d "build" ]; then
    # Reconfigure the existing build directory
    meson setup build --prefix=/usr --reconfigure
else
    meson setup build --prefix=/usr
fi

echo -e "${BLUE}Compiling the project...${NC}"
meson compile -C build

# Step 3: Clean and recreate the temporary package directory structure
echo -e "${BLUE}Preparing temporary packaging directory...${NC}"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"

# Step 4: Install build target to the DESTDIR
echo -e "${BLUE}Installing binary into DESTDIR...${NC}"
DESTDIR="$(pwd)/$PKG_DIR" meson install -C build

# Step 5: Install desktop entry and SVG icon
echo -e "${BLUE}Adding desktop entry and icons...${NC}"
mkdir -p "$PKG_DIR/usr/share/applications"
mkdir -p "$PKG_DIR/usr/share/icons/hicolor/scalable/apps"

cp assets/vcodex.desktop "$PKG_DIR/usr/share/applications/"
cp assets/vcodex.svg "$PKG_DIR/usr/share/icons/hicolor/scalable/apps/"

# Step 6: Create Debian metadata control file
echo -e "${BLUE}Generating DEBIAN/control file...${NC}"
mkdir -p "$PKG_DIR/DEBIAN"

cat << EOF > "$PKG_DIR/DEBIAN/control"
Package: vcodex
Version: $VERSION
Section: devel
Priority: optional
Architecture: $ARCH
Maintainer: Vcodex Team <info@vcodex.org>
Depends: libgtk-3-0 | libgtk-3-0t64, libgtksourceview-4-0, libvte-2.91-0, libsoup-3.0-0, libjson-glib-1.0-0, libgit2-glib-1.0-0, libsecret-1-0
Description: Vcodex AI-powered Code Editor
 A lightweight and modern code editor featuring an integrated virtual terminal,
 git integration, and a sophisticated AI assistant orchestration panel.
EOF

# Step 7: Build the debian package
DEB_FILE="vcodex_${VERSION}_${ARCH}.deb"
echo -e "${BLUE}Building deb package: $DEB_FILE...${NC}"

# Modern dpkg-deb versions support --root-owner-group to avoid building packages
# with non-root ownership on build files.
dpkg-deb --root-owner-group --build "$PKG_DIR" "$DEB_FILE"

echo -e "${GREEN}=== Package build completed successfully ===${NC}"
echo -e "You can install the package using: ${BLUE}sudo dpkg -i $DEB_FILE${NC}"
echo -e "Followed by: ${BLUE}sudo apt-get install -f${NC} (to fix any missing dependencies)"
