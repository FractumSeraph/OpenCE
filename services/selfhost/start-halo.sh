#!/bin/sh
# Starts the Halo web server from this folder (Linux x64). Ctrl+C stops it.
cd "$(dirname "$0")" || exit 1

# Copying the folder from Windows drops the executable bits; restore them.
chmod +x server/runtime/linux-x64/node \
  server/bin/halo-native-gateway-linux-x64 \
  server/node_modules/@cloudflare/workerd-linux-64/bin/workerd 2>/dev/null

node_bin=node
if [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ] &&
   [ -x server/runtime/linux-x64/node ]; then
  node_bin=server/runtime/linux-x64/node
fi
exec "$node_bin" server/server.mjs
