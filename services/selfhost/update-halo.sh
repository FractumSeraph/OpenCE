#!/bin/sh
# Updates this folder (the Linux kit, run by the systemd service "halo" as in
# HOSTING-VPS.md) to the newest kit, keeping config.json and data/. If the
# server does not answer afterwards, the previous version is put back.
#
#   sudo /opt/halo/update-halo.sh [<kit zip URL or file>]
#
# Default: the fork's web-latest release (HALO_KIT_URL overrides it).
set -eu
cd "$(dirname "$0")"
kit=${1:-${HALO_KIT_URL:-https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-server-linux-x64.zip}}
port=$(sed -n 's/.*"http": *{[^}]*"port": *\([0-9]*\).*/\1/p' config.json 2>/dev/null | head -n 1)
port=${port:-8765}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

case "$kit" in
  http://*|https://*) curl -fsSL -o "$tmp/kit.zip" "$kit" ;;
  *) cp "$kit" "$tmp/kit.zip" ;;
esac
unzip -q "$tmp/kit.zip" -d "$tmp"
[ -f "$tmp/halo-server/server/server.mjs" ] || { echo "not a Halo server kit: $kit" >&2; exit 1; }

answers() {
  for _ in $(seq 1 30); do
    curl -fs -o /dev/null "http://127.0.0.1:$port/v1/public-games" && return 0
    sleep 2
  done
  return 1
}

# (previous/: the server, and the game files the kit replaces; the maps in
# public/ are not in the kit and stay where they are)
rm -rf previous && mkdir previous
cp -a server start-halo.sh previous/
(cd "$tmp/halo-server" && find public -type f) | while read -r file; do
  [ -f "$file" ] && cp -a --parents "$file" previous/
done
systemctl stop halo
rm -rf server
cp -a "$tmp/halo-server/." .
chmod +x start-halo.sh update-halo.sh
chown -R halo:halo .
systemctl start halo
if answers; then
  echo "updated: the server answers on port $port"
else
  echo "the new version does not answer; putting the previous one back" >&2
  systemctl stop halo
  rm -rf server
  cp -a previous/. .
  chown -R halo:halo .
  systemctl start halo
  answers && echo "the previous version answers again" >&2
  exit 1
fi
