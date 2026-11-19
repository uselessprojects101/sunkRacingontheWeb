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
  var mySlot = -1;
  var statusText = 'idle';
  var idBySlot = [null, null, null, null];
  var lastSeen = [0, 0, 0, 0];
  var activeSlot = [false, false, false, false];
  var pingTimer = null;
  var checkTimer = null;

  function makeId() {
    var out = '';
    var chars = '0123456789abcdef';
    for (var i = 0; i < 8; i++) out += chars.charAt(Math.floor(Math.random() * 16));
    return out;
  }

  var clientId = makeId();

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

  function setStatus(t) {
    statusText = t;
    if (api.onStatus) api.onStatus(t);
  }

  function setSlot(n) {
    if (mySlot === n) return;
    mySlot = n;
    if (n >= 0) setStatus('you are player ' + (n + 1));
    if (api.onSlot) api.onSlot(n);
  }

  function isHello(b) {
    return b && b.length === 5 && b[0] === 0x53 && b[1] === 0x52 && b[2] === 0x4c && b[3] === 1;
  }

  function isHelloRequest(b) {
    return b && (b.length === 4 || b.length === 12) && b[0] === 0x53 && b[1] === 0x52 && b[2] === 0x4c && b[3] === 2;
  }

  function isPing(b) {
    return b && b.length === 4 && b[0] === 0x53 && b[1] === 0x52 && b[2] === 0x4c && b[3] === 3;
  }

  function helloRequest() {
    var out = new Uint8Array(12);
    out[0] = 0x53;
    out[1] = 0x52;
    out[2] = 0x4c;
    out[3] = 2;
    for (var i = 0; i < 8; i++) out[4 + i] = clientId.charCodeAt(i);
    return out;
  }

  function closeAll() {
    gen++;
    setSlot(-1);
    discovering = false;
    found = false;
    delivered = false;
    attempting = false;
    currentFail = null;
    if (retryTimer) {
      clearTimeout(retryTimer);
      retryTimer = null;
    }
    if (pingTimer) {
      clearInterval(pingTimer);
      pingTimer = null;
    }
    if (checkTimer) {
      clearInterval(checkTimer);
      checkTimer = null;
    }
    for (var i = 0; i < MAX; i++) {
      if (conns[i]) {
        try { conns[i].close(); } catch (e) {}
        conns[i] = null;
      }
      idBySlot[i] = null;
      lastSeen[i] = 0;
      activeSlot[i] = false;
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

  function dropSlot(slot, why) {
    var c = conns[slot];
    conns[slot] = null;
    activeSlot[slot] = false;
    lastSeen[slot] = 0;
    if (c) {
      try { c.close(); } catch (e) {}
    }
    if (why) setStatus(why + ' ' + slot);
  }

  function onGuest(conn) {
    var bound = -1;
    var tellSlot = function () {
      if (bound < 0) return;
      try { conn.send(new Uint8Array([0x53, 0x52, 0x4c, 1, bound])); } catch (e) {}
    };
    var bind = function (id) {
      if (bound !== -1) return;
      var slot = -1;
      var i;
      if (id) {
        for (i = 1; i < MAX; i++) {
          if (idBySlot[i] === id) {
            slot = i;
            break;
          }
        }
      }
      if (slot < 0) {
        for (i = 1; i < MAX; i++) {
          if (!conns[i]) {
            slot = i;
            break;
          }
        }
      }
      if (slot < 0) {
        bound = -2;
        try { conn.close(); } catch (e) {}
        return;
      }
      if (conns[slot] && conns[slot] !== conn) {
        var old = conns[slot];
        conns[slot] = null;
        try { old.close(); } catch (e2) {}
      }
      bound = slot;
      conns[slot] = conn;
      idBySlot[slot] = id || null;
      activeSlot[slot] = false;
      lastSeen[slot] = Date.now();
      setStatus('guest connected in slot ' + slot);
      tellSlot();
    };
    conn.on('data', function (d) {
      var b = toBytes(d);
      if (bound >= 0 && conns[bound] !== conn) return;
      if (isHelloRequest(b)) {
        var id = null;
        if (b.length === 12) {
          id = '';
          for (var k = 4; k < 12; k++) id += String.fromCharCode(b[k]);
        }
        if (bound === -1) bind(id);
        else tellSlot();
        if (bound >= 0) lastSeen[bound] = Date.now();
        return;
      }
      if (bound === -2) return;
      if (bound === -1) bind(null);
      if (bound < 0) return;
      lastSeen[bound] = Date.now();
      if (isPing(b)) return;
      activeSlot[bound] = true;
      push(bound, d);
    });
    conn.on('close', function () {
      if (bound >= 0 && conns[bound] === conn) {
        conns[bound] = null;
        activeSlot[bound] = false;
        lastSeen[bound] = 0;
        setStatus('guest left slot ' + bound);
      }
    });
    conn.on('error', function () {
      if (bound >= 0 && conns[bound] === conn) {
        conns[bound] = null;
        activeSlot[bound] = false;
        lastSeen[bound] = 0;
      }
    });
  }

  function startCheck() {
    var every = window.SONICR_NET_CHECK || 3000;
    var limit = window.SONICR_NET_TIMEOUT || 20000;
    checkTimer = setInterval(function () {
      var now = Date.now();
      for (var i = 1; i < MAX; i++) {
        if (conns[i] && lastSeen[i] && now - lastSeen[i] > limit) dropSlot(i, 'guest timed out in slot');
      }
    }, every);
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
    setStatus('starting host');
    startCheck();
    peer.on('open', function () {
      if (g === gen) setStatus('hosting, waiting for players');
    });
    peer.on('connection', function (conn) {
      if (g === gen) onGuest(conn);
    });
    peer.on('error', function (e) {
      if (g !== gen) return;
      if (e && e.type === 'unavailable-id') {
        setStatus('this link already has a host');
        report('this link already has a host, use a different #name');
      }
      else report('host error: ' + (e && e.type ? e.type : e));
    });
    return 0;
  }

  function connectHost() {
    if (!discovering || found || attempting || !peer || !peer.open) return;
    var g = gen;
    attempting = true;
    setStatus('connecting to host');
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
      if (g === gen) {
        setStatus('searching for host');
        retryTimer = setTimeout(connectHost, 2000);
      }
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
      setStatus('connected, waiting for player number');
      try { c.send(helloRequest()); } catch (e) {}
      if (pingTimer) clearInterval(pingTimer);
      pingTimer = setInterval(function () {
        if (hostConn === c && c.open) {
          try { c.send(new Uint8Array([0x53, 0x52, 0x4c, 3])); } catch (e) {}
        }
      }, window.SONICR_NET_PING || 3000);
      c.on('data', function (d) {
        var b = toBytes(d);
        if (isHello(b)) {
          setSlot(b[4]);
          return;
        }
        push(-1, d);
      });
      c.on('close', function () {
        if (hostConn === c) {
          hostConn = null;
          if (pingTimer) {
            clearInterval(pingTimer);
            pingTimer = null;
          }
          setSlot(-1);
          if (!delivered && discovering && g === gen) {
            found = false;
            setStatus('lost the host, searching again');
            retryTimer = setTimeout(connectHost, 1000);
          }
        }
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
    setStatus('searching for host');
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
    return c && c.open && activeSlot[slot] ? 1 : 0;
  }

  function unregister(slot) {
    dropSlot(slot, '');
  }

  function pop() {
    return queue.shift() || null;
  }

  var api = {
    version: 3,
    setClientId: function (id) { clientId = id; },
    status: function () { return statusText; },
    onStatus: null,
    onSlot: null,
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
  window.SonicRNet = api;
})(window, location);
