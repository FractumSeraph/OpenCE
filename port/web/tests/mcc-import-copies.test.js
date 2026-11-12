'use strict';
// (this fork's browser build) The page's "Add MCC maps" (shell.html) runs
// OpenCE's MCC importer from port/web/assets/mcc-import, which every way of
// packaging the page copies (tools/browser_build.py, tools/web_package.py):
// copies of OpenCE's port/web/site files, which must stay the same as them
// when a merge changes OpenCE's.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const web = path.join(__dirname, '..');

for (const name of ['map-import-files.js', 'map-import-storage.js', 'mcc-import-worker.js']) {
  test(`assets/mcc-import/${name} is OpenCE's site/${name}`, () => {
    const copy = fs.readFileSync(path.join(web, 'assets', 'mcc-import', name), 'utf8').replace(/\r\n/g, '\n');
    const original = fs.readFileSync(path.join(web, 'site', name), 'utf8').replace(/\r\n/g, '\n');
    assert.equal(copy, original, `copy port/web/site/${name} to port/web/assets/mcc-import/ again`);
  });
}
