'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const repository = path.join(__dirname, '..', '..', '..');
const buildRules = fs.readFileSync(
  path.join(repository, 'tools', 'browser_build.py'), 'utf8');
const sdlPort = fs.readFileSync(
  path.join(repository, 'port', 'web', 'halo_sdl3.py'), 'utf8');

assert.match(buildRules, /WEB_SDL_FLAG = f"--use-port=\{WEB_SDL_PORT\}"/,
  'the browser build must use the source-controlled SDL port');
assert.doesNotMatch(buildRules, /-sUSE_SDL=3/,
  'the build must not silently fall back to Emscripten\'s SDL 3.4.2 port');
assert.match(sdlPort, /VERSION = "3\.4\.16"/,
  'SDL 3.4.16 contains the main-thread gamepad metadata fix');
assert.match(sdlPort, /sha512hash=HASH/,
  'the downloaded SDL archive must be integrity checked');

// When a build exists, also guard the emitted worker runtime itself. SDL
// 3.4.2 emitted this direct worker call; fixed SDL proxies it to the main
// browser thread instead.
const generatedRuntime = path.join(repository, 'build', 'web', 'halo.js');
if (fs.existsSync(generatedRuntime)) {
  const javascript = fs.readFileSync(generatedRuntime, 'utf8');
  assert.doesNotMatch(javascript,
    /function SDL_GetEmscriptenJoystickVendor\([^)]*\)\{[^}]*navigator\["getGamepads"\]/,
    'SDL gamepad metadata must not call navigator.getGamepads in the worker');
}

console.log('threaded SDL gamepad proxy tests passed');
