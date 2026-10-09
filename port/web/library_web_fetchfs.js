/**
 * The maps' file system (WasmFS's fetch backend: web_platform.c mounts the
 * Xbox maps and the Custom Edition maps with it), Emscripten's own
 * (src/lib/libwasmfs_fetch.js, MIT, as of 6.0.11) with two changes. A user
 * JS library is linked after Emscripten's, so this one replaces it.
 *
 * - A file's size alone (getSize: stat, the game looking for a file) costs
 *   no download. Emscripten's asked for its size with a HEAD and then, its
 *   length 0 working out to chunk 0, fetched the file's first chunk anyway,
 *   or a small file whole: listing a hundred Custom Edition maps downloaded
 *   over 30 MB (each map's first 256 KB) before a phone's menus answered.
 *   The size comes from the HEAD alone now (this site's page answers the
 *   Custom Edition maps' HEADs from their index.json: fetch_path_normalization.js),
 *   and the data is fetched on the first read.
 * - A file kept whole (a small one, or a server without byte ranges) is
 *   read from that copy on every read: Emscripten's, past the first read,
 *   counted its chunks at the mount's chunk size against the whole file's
 *   length and asked again for pieces it had.
 */

addToLibrary({
  $wasmFS$JSMemoryRanges: {},

  _wasmfs_create_fetch_backend_js__deps: [
    '$wasmFS$backends',
    '$wasmFS$JSMemoryRanges',
    '_wasmfs_fetch_get_file_url',
    '_wasmfs_fetch_get_chunk_size',
  ],
  _wasmfs_create_fetch_backend_js: async function(backend) {
    function fileUrl(file) {
      var name = UTF8ToString(__wasmfs_fetch_get_file_url(file));
      if (name.indexOf('://') !== -1) return name;
      try {
        return new URL(name, self.location.origin).toString();
      } catch (_e) {
        throw {status: 404};
      }
    }

    // the whole file fetched, into its one chunk
    async function fetchWhole(file, url) {
      var wholeFileReq = await fetch(url);
      if (!wholeFileReq.ok) {
        throw wholeFileReq;
      }
      var wholeFileData = new Uint8Array(await wholeFileReq.arrayBuffer());
      wasmFS$JSMemoryRanges[file] = {
        size: wholeFileData.byteLength,
        chunks: [wholeFileData],
        chunkSize: wholeFileData.byteLength,
        whole: true,
      };
    }

    // the file's bytes from offset, len of them, fetched into JS memory if
    // they are not there yet; len 0: its size alone
    async function getFileRange(file, offset, len) {
      var url = fileUrl(file);
      var chunkSize = __wasmfs_fetch_get_chunk_size(file);
      offset ??= 0;
      len ??= chunkSize;
      if (!(file in wasmFS$JSMemoryRanges) || !wasmFS$JSMemoryRanges[file]) {
        var fileInfo = await fetch(url, {method:'HEAD', headers:{'Range': 'bytes=0-'}});
        var known = fileInfo.ok && fileInfo.headers.has('Content-Length');
        var size = known ? parseInt(fileInfo.headers.get('Content-Length'), 10) : 0;
        if (known && fileInfo.headers.get('Accept-Ranges') == 'bytes' && size > chunkSize*2) {
          wasmFS$JSMemoryRanges[file] = {
            size,
            chunks: [],
            chunkSize: chunkSize
          };
        } else if (known && len === 0) {
          // (kept whole once read: its size for now)
          wasmFS$JSMemoryRanges[file] = {
            size,
            chunks: [],
            chunkSize: size,
            whole: true,
          };
          return;
        } else {
          // may as well/forced to download the whole file
          await fetchWhole(file, url);
          return;
        }
      }
      var info = wasmFS$JSMemoryRanges[file];
      if (len === 0) {
        return;
      }
      if (info.whole) {
        if (!info.chunks[0]) {
          await fetchWhole(file, url);
        }
        return;
      }
      len = Math.min(len, info.size - offset);
      if (len <= 0) {
        return;
      }
      // In which chunks does the range start and end? E.g. 5-14 with chunk
      // size 8 is in chunks 0 and 1.
      var firstChunk = (offset / chunkSize) | 0;
      var lastChunk = ((offset+len-1) / chunkSize) | 0;
      var allPresent = true;
      var i;
      for (i = firstChunk; i <= lastChunk; i++) {
        if (!info.chunks[i]) {
          allPresent = false;
          break;
        }
      }
      if (allPresent) {
        return;
      }
      // one request for all the chunks needed, up to the last byte of the
      // last chunk
      var start = firstChunk * chunkSize;
      var end = (lastChunk+1) * chunkSize;
      var response = await fetch(url, {headers:{'Range': `bytes=${start}-${end-1}`}});
      if (!response.ok) {
        throw response;
      }
      var bytes = new Uint8Array(await response.arrayBuffer());
      for (i = firstChunk; i <= lastChunk; i++) {
        info.chunks[i] = bytes.slice(i*chunkSize-start,(i+1)*chunkSize-start);
      }
    }

    wasmFS$backends[backend] = {
      allocFile: async (file) => { /* nop */ },
      freeFile: async (file) => {
        wasmFS$JSMemoryRanges[file] = undefined;
      },

      write: async (file, buffer, length, offset) => {
        console.error('TODO: file writing in fetch backend? read-only for now');
      },

      read: async (file, buffer, length, offset) => {
        if (offset < 0 || length <= 0) {
          return 0;
        }
        try {
          await getFileRange(file, offset || 0, length);
        } catch (failedResponse) {
          return failedResponse.status === 404 ? -{{{ cDefs.ENOENT }}} : -{{{ cDefs.EBADF }}};
        }
        var fileInfo = wasmFS$JSMemoryRanges[file];
        length = Math.min(length, fileInfo.size-offset);
        if (length <= 0) {
          return 0;
        }
        var chunks = fileInfo.chunks;
        var chunkSize = fileInfo.chunkSize;
        var firstChunk = (offset / chunkSize) | 0;
        var lastChunk = ((offset+length-1) / chunkSize) | 0;
        var readLength = 0;
        for (var i = firstChunk; i <= lastChunk; i++) {
          var chunk = chunks[i];
          var start = Math.max(i*chunkSize, offset);
          var chunkStart = i*chunkSize;
          var end = Math.min(chunkStart+chunkSize, offset+length);
          HEAPU8.set(chunk.subarray(start-chunkStart, end-chunkStart), buffer+(start-offset));
          readLength = end - offset;
        }
        return readLength;
      },
      getSize: async (file) => {
        try {
          await getFileRange(file, 0, 0);
        } catch (failedResponse) {
          return 0;
        }
        return wasmFS$JSMemoryRanges[file].size;
      },
    };
  },
});
