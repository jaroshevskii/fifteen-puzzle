#!/bin/sh
# Wraps the FifteenPuzzle binary in a macOS .app bundle with an icon, so the
# release is a double-clickable app (and runs from the itch.io app) instead of a
# bare CLI executable. macOS-only: uses `sips` + `iconutil`, both built in.
#
#   Bootstrap/make-macos-app.sh <binary> <icon.png> <output.app> [version]
#
# The .app is unsigned — see the itch page notes on Gatekeeper (right-click →
# Open, or `xattr -dr com.apple.quarantine`).
set -eu

BIN="$1"       # path to the built FifteenPuzzle executable
ICON_PNG="$2"  # path to a square PNG (1024x1024 recommended)
APP="$3"       # output bundle path, e.g. dist/FifteenPuzzle.app
VERSION="${4:-0.0.0}"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

cp "$BIN" "$APP/Contents/MacOS/FifteenPuzzle"
chmod +x "$APP/Contents/MacOS/FifteenPuzzle"

# PNG -> .iconset (all the sizes macOS wants) -> AppIcon.icns.
iconset="$(mktemp -d)/AppIcon.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
  sips -z "$size" "$size" "$ICON_PNG" --out "$iconset/icon_${size}x${size}.png" >/dev/null
  retina=$((size * 2))
  sips -z "$retina" "$retina" "$ICON_PNG" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$APP/Contents/Resources/AppIcon.icns"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>15 Puzzle</string>
  <key>CFBundleDisplayName</key><string>15 Puzzle</string>
  <key>CFBundleExecutable</key><string>FifteenPuzzle</string>
  <key>CFBundleIdentifier</key><string>tech.gen.fifteenpuzzle</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

echo "created $APP (version ${VERSION})"
