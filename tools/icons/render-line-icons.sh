#!/bin/sh
# Renders the line icons (data/icons/line/svg/*.svg) to the PNGs Clementine
# ships, and writes the matching <file> entries for data/data.qrc to
# data/icons/line/qrc-entries.txt. After adding or renaming an icon, replace
# the icons/line entries in data/data.qrc with that file's contents, then
# delete it.
#
# The icons are drawn in black: Clementine only uses their shape, and paints
# them in the theme's text colour (see LineIcon() in src/ui/symbolicicon.cpp).
# Shipping PNGs rather than the SVGs keeps Qt's SVG module out of the
# dependencies.
#
#   tools/icons/render-line-icons.sh
#
# Needs rsvg-convert (librsvg2-bin on Debian and Ubuntu).
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
src=$root/data/icons/line/svg
out=$root/data/icons/line
sizes="16 24 32 48 96"

# Other names Clementine asks for that draw the same icon.
aliases="
x-clementine-shuffle media-playlist-shuffle
x-clementine-albums x-clementine-album
media-optical x-clementine-album
audio-x-generic folder-sound
search system-search
edit-find system-search
document-open-folder folder
about-info help-about
help-hint help-about
"

entries=$out/qrc-entries.txt
: > "$entries"

for size in $sizes; do
  mkdir -p "$out/$size"
  for svg in "$src"/*.svg; do
    name=$(basename "$svg" .svg)
    rsvg-convert -w "$size" -h "$size" -o "$out/$size/$name.png" "$svg"
    echo "        <file>icons/line/$size/$name.png</file>" >> "$entries"
  done
  echo "$aliases" | while read -r alias target; do
    [ -n "$alias" ] || continue
    echo "        <file alias=\"icons/line/$size/$alias.png\">icons/line/$size/$target.png</file>" >> "$entries"
  done
done

echo "Rendered $(ls "$src"/*.svg | wc -l) icons at: $sizes"
echo "qrc entries: $entries"
