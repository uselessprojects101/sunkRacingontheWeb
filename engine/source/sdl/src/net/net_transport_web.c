#include "net_transport.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>

static int s_active = 0;
static int s_isHost = 0;
static int s_localSlot = 0;

EM_JS(int, js_net_host_start, (void), {
    return window.SonicRNet ? window.SonicRNet.hostStart() : -1;
});

EM_JS(int, js_net_discover_send, (void), {
    return window.SonicRNet ? window.SonicRNet.discoverSend() : -1;
});

EM_JS(int, js_net_discover_check, (void), {
    return window.SonicRNet ? window.SonicRNet.discoverCheck() : 0;
});

EM_JS(int, js_net_client_ready, (void), {
    return window.SonicRNet ? window.SonicRNet.clientReady() : 0;
});

EM_JS(int, js_net_slot_connected, (int slot), {
    return window.SonicRNet ? window.SonicRNet.slotConnected(slot) : 0;
});

EM_JS(void, js_net_unregister, (int slot), {
    if (window.SonicRNet) window.SonicRNet.unregister(slot);
});

EM_JS(void, js_net_close, (void), {
    if (window.SonicRNet) window.SonicRNet.close();
});

EM_JS(int, js_net_send, (int slot, const void *data, int len), {
    if (!window.SonicRNet) return -1;
    return window.SonicRNet.send(slot, HEAPU8.slice(data, data + len));
});

EM_JS(int, js_net_recv, (void *buf, int maxlen, int *slotOut), {
    if (!window.SonicRNet) return 0;
    var item = window.SonicRNet.pop();
    if (!item) return 0;
    var n = Math.min(item.bytes.length, maxlen);
    HEAPU8.set(item.bytes.subarray(0, n), buf);
    HEAP32[slotOut >> 2] = item.slot;
    return n;
});

int net_transport_init(void)
{
    s_active = 0;
    s_isHost = 0;
    s_localSlot = 0;
    return 0;
}

int net_host_start(int port)
{
    (void)port;
    if (s_active && s_isHost) return 0;
    if (s_active) net_close();
    if (js_net_host_start() != 0) return -1;
    s_isHost = 1;
    s_active = 1;
    s_localSlot = 0;
    return 0;
}

int net_client_connect(const char *host_ip, int port)
{
    (void)host_ip;
    (void)port;
    if (!js_net_client_ready()) return -1;
    s_isHost = 0;
    s_active = 1;
    s_localSlot = -1;
    return 0;
}

int net_send_to_host(const void *data, int len)
{
    if (!s_active) return -1;
    if (s_isHost) return len;
    return js_net_send(-1, data, len);
}

int net_broadcast(const void *data, int len)
{
    if (!s_active) return -1;
    if (!s_isHost) return net_send_to_host(data, len);
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        if (js_net_slot_connected(i)) js_net_send(i, data, len);
    }
    return 0;
}

int net_send_to(int player_slot, const void *data, int len)
{
    if (!s_active) return -1;
    if (player_slot < 0 || player_slot >= NET_MAX_PLAYERS) return -1;
    if (player_slot == s_localSlot) return len;
    if (!s_isHost) return -1;
    if (!js_net_slot_connected(player_slot)) return -1;
    return js_net_send(player_slot, data, len);
}

int net_recv(void *buf, int maxlen, int *from_slot)
{
    if (!s_active) return 0;
    int slot = -1;
    int n = js_net_recv(buf, maxlen, &slot);
    if (n <= 0) return 0;
    if (from_slot) *from_slot = slot;
    return n;
}

int net_register_client(int slot)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return -1;
    return slot;
}

int net_discover_send(int port)
{
    (void)port;
    return js_net_discover_send() == 0 ? 0 : -1;
}

int net_discover_check(char *host_ip, int host_ip_len)
{
    if (js_net_discover_check() != 1) return 0;
    snprintf(host_ip, (size_t)host_ip_len, "10.0.0.1");
    return 1;
}

int net_is_active(void)
{
    return s_active;
}

int net_is_host(void)
{
    return s_isHost;
}

int net_local_slot(void)
{
    return s_localSlot;
}

void net_set_local_slot(int slot)
{
    s_localSlot = slot;
}

void net_close(void)
{
    js_net_close();
    s_active = 0;
    s_isHost = 0;
    s_localSlot = 0;
}

void net_unregister_slot(int slot)
{
    if (slot <= 0 || slot >= NET_MAX_PLAYERS) return;
    js_net_unregister(slot);
}

int net_slot_is_connected(int slot)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return 0;
    if (!s_isHost) return 0;
    if (slot == 0) return s_active ? 1 : 0;
    return js_net_slot_connected(slot);
}

int net_probe_ping(const char *ip, int port, int timeout_ms)
{
    (void)ip;
    (void)port;
    (void)timeout_ms;
    return -1;
}
