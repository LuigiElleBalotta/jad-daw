#!/bin/sh
# Builds the macOS disk image from a built, deployed app: installer/macos/build-dmg.sh <app> <version> <out dir>
# The image holds the app and a link to /Applications to drag it onto. (The archive that the updater uses is made by the release workflow.)
set -eu
app="$1"; version="$2"; out="$3"
stage="$(mktemp -d)"
cp -R "$app" "$stage/"
ln -s /Applications "$stage/Applications"
mkdir -p "$out"
hdiutil create -volname "JAD Daw $version" -srcfolder "$stage" -ov -format UDZO "$out/jad-daw-$version-macos-arm64.dmg"
rm -rf "$stage"
