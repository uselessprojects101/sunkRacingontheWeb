(function (window) {
  var DB_NAME = 'sunkr';
  var ROOTS = ['/game/SAVE', '/game/GHOST'];
  var MARK = '/game/.complete';
  var dbPromise = null;
  var known = {};
  var timer = null;
  var reported = false;

  function available() {
    return !!window.indexedDB;
  }

  function open() {
    if (dbPromise) return dbPromise;
    dbPromise = new Promise(function (resolve, reject) {
      var req = window.indexedDB.open(DB_NAME, 1);
      req.onupgradeneeded = function () {
        var db = req.result;
        if (!db.objectStoreNames.contains('data')) db.createObjectStore('data');
        if (!db.objectStoreNames.contains('saves')) db.createObjectStore('saves');
      };
      req.onsuccess = function () { resolve(req.result); };
      req.onerror = function () {
        dbPromise = null;
        reject(req.error);
      };
    });
    return dbPromise;
  }

  function txDone(tx) {
    return new Promise(function (resolve, reject) {
      tx.oncomplete = function () { resolve(); };
      tx.onerror = function () { reject(tx.error); };
      tx.onabort = function () { reject(tx.error || new Error('aborted')); };
    });
  }

  function put(storeName, key, value) {
    return open().then(function (db) {
      var tx = db.transaction(storeName, 'readwrite');
      tx.objectStore(storeName).put(value, key);
      return txDone(tx);
    });
  }

  function del(storeName, key) {
    return open().then(function (db) {
      var tx = db.transaction(storeName, 'readwrite');
      tx.objectStore(storeName).delete(key);
      return txDone(tx);
    });
  }

  function clearStore(storeName) {
    return open().then(function (db) {
      var tx = db.transaction(storeName, 'readwrite');
      tx.objectStore(storeName).clear();
      return txDone(tx);
    });
  }

  function hasData() {
    return open().then(function (db) {
      return new Promise(function (resolve, reject) {
        var req = db.transaction('data', 'readonly').objectStore('data').count(MARK);
        req.onsuccess = function () { resolve(req.result > 0); };
        req.onerror = function () { reject(req.error); };
      });
    });
  }

  function writeTo(FS, path, value) {
    var bytes = value instanceof Uint8Array ? value : new Uint8Array(value);
    FS.mkdirTree(path.substring(0, path.lastIndexOf('/')));
    FS.writeFile(path, bytes);
  }

  function restore(storeName, FS, onProgress) {
    return open().then(function (db) {
      return new Promise(function (resolve, reject) {
        var count = 0;
        var req = db.transaction(storeName, 'readonly').objectStore(storeName).openCursor();
        req.onsuccess = function () {
          var cur = req.result;
          if (!cur) {
            resolve(count);
            return;
          }
          try {
            writeTo(FS, cur.key, cur.value);
          } catch (e) {
            reject(e);
            return;
          }
          count++;
          if (onProgress) onProgress(count);
          cur.continue();
        };
        req.onerror = function () { reject(req.error); };
      });
    });
  }

  function stamp(FS, p) {
    var st = FS.stat(p);
    var m = st.mtime && st.mtime.getTime ? st.mtime.getTime() : st.mtime;
    return m + ':' + st.size;
  }

  function listFiles(FS, root, out) {
    if (!FS.analyzePath(root).exists) return;
    var names = FS.readdir(root);
    for (var i = 0; i < names.length; i++) {
      var n = names[i];
      if (n === '.' || n === '..') continue;
      var p = root + '/' + n;
      if (FS.isDir(FS.stat(p).mode)) listFiles(FS, p, out);
      else out.push(p);
    }
  }

  function listTop(FS, out) {
    if (!FS.analyzePath('/game').exists) return;
    var names = FS.readdir('/game');
    for (var i = 0; i < names.length; i++) {
      var n = names[i];
      if (n === '.' || n === '..' || n === '.complete') continue;
      var p = '/game/' + n;
      if (!FS.isDir(FS.stat(p).mode)) out.push(p);
    }
  }

  function syncNow(FS) {
    var files = [];
    for (var r = 0; r < ROOTS.length; r++) listFiles(FS, ROOTS[r], files);
    listTop(FS, files);
    var present = {};
    var jobs = [];
    files.forEach(function (p) {
      present[p] = true;
      var s = stamp(FS, p);
      if (known[p] === s) return;
      known[p] = s;
      jobs.push(put('saves', p, FS.readFile(p).slice()));
    });
    Object.keys(known).forEach(function (p) {
      if (present[p]) return;
      delete known[p];
      jobs.push(del('saves', p));
    });
    return Promise.all(jobs);
  }

  function restoreSaves(FS) {
    return restore('saves', FS).then(function (count) {
      var files = [];
      for (var r = 0; r < ROOTS.length; r++) {
        FS.mkdirTree(ROOTS[r]);
        listFiles(FS, ROOTS[r], files);
      }
      listTop(FS, files);
      files.forEach(function (p) { known[p] = stamp(FS, p); });
      return count;
    });
  }

  function startSaveSync(FS) {
    if (timer) return;
    var run = function () {
      try {
        syncNow(FS).catch(function (e) {
          if (!reported && typeof window.showErr === 'function') {
            reported = true;
            window.showErr('saves could not be stored: ' + e);
          }
        });
      } catch (e2) {}
    };
    timer = setInterval(run, 8000);
    if (window.document) {
      window.document.addEventListener('visibilitychange', function () {
        if (window.document.visibilityState === 'hidden') run();
      });
    }
    window.addEventListener('pagehide', run);
  }

  window.SonicRStore = {
    available: available,
    hasData: hasData,
    saveData: function (path, bytes) { return put('data', path, bytes); },
    markComplete: function () { return put('data', MARK, new Uint8Array([1])); },
    clearData: function () { return clearStore('data'); },
    restoreData: function (FS, onProgress) { return restore('data', FS, onProgress); },
    restoreSaves: restoreSaves,
    startSaveSync: startSaveSync,
    syncNow: syncNow
  };
})(window);
