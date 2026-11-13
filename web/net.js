(function (window, location) {
  var MAX = 4;
  var queue = [];
  var conns = [null, null, null, null];
  var hostConn = null;
  var peer = null;
  var discovering = false;
  var found = false;
  var delivered = false;
  var attempting = false;
  var retryTimer = null;
  var currentFail = null;
  var gen = 0;

  function report(text) {
    if (typeof window.showErr === 'function') window.showErr('lan: ' + text);
    else if (window.console) window.console.warn('lan: ' + text);
  }

  function roomName() {
    var h = String(location.hash || '').replace(/^#/, '').toLowerCase().replace(/[^a-z0-9_-]/g, '').slice(0, 40);
    return h || 'lobby';
  }

  function hostId() {
    return 'sunkr-' + roomName() + '-host';
  }

  function newPeer(id) {
    var opts = window.SONICR_PEER_OPTIONS || {};
    return id ? new window.Peer(id, opts) : new window.Peer(opts);
  }

  function toBytes(d) {
    if (d instanceof ArrayBuffer) return new Uint8Array(d);
    if (ArrayBuffer.isView(d)) return new Uint8Array(d.buffer, d.byteOffset, d.byteLength);
    return null;
  }

  function push(slot, d) {
    var b = toBytes(d);
    if (!b) return;
    if (queue.length >= 512) queue.shift();
    queue.push({ slot: slot, bytes: b });
  }

  function closeAll() {
    gen++;
    discovering = false;
    found = false;
    delivered = false;
    attempting = false;
    currentFail = null;
    if (retryTimer) {
      clearTimeout(retryTimer);
      retryTimer = null;
    }
    for (var i = 0; i < MAX; i++) {
      if (conns[i]) {
        try { conns[i].close(); } catch (e) {}
        conns[i] = null;
      }
    }
    if (hostConn) {
      try { hostConn.close(); } catch (e2) {}
      hostConn = null;
    }
    if (peer) {
      try { peer.destroy(); } catch (e3) {}
      peer = null;
    }
    queue.length = 0;
  }

  function onGuest(conn) {
    var slot = -1;
    for (var i = 1; i < MAX; i++) {
      if (!conns[i]) {
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      conn.on('open', function () {
        try { conn.close(); } catch (e) {}
      });
      return;
    }
    conns[slot] = conn;
    conn.on('data', function (d) {
      push(slot, d);
    });
    conn.on('close', function () {
      if (conns[slot] === conn) conns[slot] = null;
    });
    conn.on('error', function () {
      if (conns[slot] === conn) conns[slot] = null;
    });
  }

  function hostStart() {
    closeAll();
    if (!window.Peer) {
      report('peerjs did not load');
      return -1;
    }
    var g = gen;
    try {
      peer = newPeer(hostId());
    } catch (e) {
      report(String(e));
      return -1;
    }
    peer.on('connection', function (conn) {
      if (g === gen) onGuest(conn);
    });
    peer.on('error', function (e) {
      if (g !== gen) return;
      if (e && e.type === 'unavailable-id') report('this link already has a host, use a different #name');
      else report('host error: ' + (e && e.type ? e.type : e));
    });
    return 0;
  }

  function connectHost() {
    if (!discovering || found || attempting || !peer || !peer.open) return;
    var g = gen;
    attempting = true;
    var c = peer.connect(hostId(), { reliable: true, serialization: 'raw' });
    var finished = false;
    var timer = null;
    function fail() {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      attempting = false;
      currentFail = null;
      try { c.close(); } catch (e) {}
      if (g === gen) retryTimer = setTimeout(connectHost, 2000);
    }
    currentFail = fail;
    timer = setTimeout(fail, 8000);
    c.on('open', function () {
      if (finished || g !== gen) return;
      finished = true;
      clearTimeout(timer);
      attempting = false;
      currentFail = null;
      hostConn = c;
      found = true;
      c.on('data', function (d) {
        push(-1, d);
      });
      c.on('close', function () {
        if (hostConn === c) hostConn = null;
      });
    });
    c.on('error', fail);
  }

  function discoverSend() {
    if (found || discovering) return 0;
    if (!window.Peer) {
      report('peerjs did not load');
      return -1;
    }
    discovering = true;
    delivered = false;
    var g = gen;
    if (!peer) {
      try {
        peer = newPeer(null);
      } catch (e) {
        report(String(e));
        discovering = false;
        return -1;
      }
      peer.on('open', function () {
        if (g === gen) connectHost();
      });
      peer.on('error', function (e) {
        if (g !== gen) return;
        if (e && e.type === 'peer-unavailable') {
          if (currentFail) currentFail();
        } else {
          report('join error: ' + (e && e.type ? e.type : e));
        }
      });
    } else if (peer.open) {
      connectHost();
    }
    return 0;
  }

  function discoverCheck() {
    if (found && !delivered) {
      delivered = true;
      discovering = false;
      return 1;
    }
    return 0;
  }

  function clientReady() {
    return hostConn && hostConn.open ? 1 : 0;
  }

  function send(slot, bytes) {
    var c = slot < 0 ? hostConn : conns[slot];
    if (!c || !c.open) return -1;
    try {
      c.send(bytes);
    } catch (e) {
      return -1;
    }
    return bytes.length;
  }

  function slotConnected(slot) {
    var c = conns[slot];
    return c && c.open ? 1 : 0;
  }

  function unregister(slot) {
    var c = conns[slot];
    conns[slot] = null;
    if (c) {
      try { c.close(); } catch (e) {}
    }
  }

  function pop() {
    return queue.shift() || null;
  }

  window.SonicRNet = {
    hostStart: hostStart,
    discoverSend: discoverSend,
    discoverCheck: discoverCheck,
    clientReady: clientReady,
    send: send,
    slotConnected: slotConnected,
    unregister: unregister,
    pop: pop,
    close: closeAll
  };
})(window, location);
