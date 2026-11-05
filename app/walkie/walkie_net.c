#include "app_config.h"
#include "system/includes.h"
#include "lwip/sockets.h"
#include "lwip.h"
#include "wifi/wifi_connect.h"
#include "syscfg/syscfg_id.h"
#include "walkie_priv.h"

#include <string.h>
#include <stdio.h>

#define WALKIE_MAGIC  0x574C4B31u /* 'WLK1' */
#define DISC_IVAL_MS  1500
#define PEER_TTL_MS   20000 /* 发长语音时 disc 可能被挤占, TTL 放宽防误踢 */
#define WALKIE_CONTACT_CFG_ID  52
#define WALKIE_CONTACT_MAGIC   0x57434E31u /* 'WCN1' */

#pragma pack(push, 1)
struct walkie_hdr {
    u32 magic;
    u8 type;
    u8 flags;
    u16 seq;
    u16 payload_len;
};
struct walkie_disc {
    char ver[WALKIE_VER_LEN];
    char name[WALKIE_PEER_NAME_LEN];
    u8 mac[6]; /* 稳定身份; 旧包无此项时按 IP/名字匹配 */
};
struct walkie_contact_item {
    u8 mac[6];
    char name[WALKIE_PEER_NAME_LEN];
};
struct walkie_contact_store {
    u32 magic;
    u8 n;
    u8 reserved[3];
    struct walkie_contact_item items[WALKIE_MAX_PEERS];
};
#pragma pack(pop)

static int s_sock = -1;
static u16 s_seq;
static volatile u8 s_net_run;
static volatile u8 s_force_disc;
static volatile u8 s_scan_burst; /* UI 扫描: 连续多发发现包 */
static OS_MUTEX s_peer_lock;
static u8 s_peer_lock_inited;
static struct walkie_peer s_peers[WALKIE_MAX_PEERS];
static int s_sel = -1;
static u8 s_dirty;
static u32 s_self_ip;
static u8 s_self_mac[6];
static char s_self_name[WALKIE_PEER_NAME_LEN];
static struct walkie_contact_store s_contacts;
static u8 s_contacts_loaded;

static u32 walkie_now_ms(void)
{
    return timer_get_ms();
}

static u8 mac_eq(const u8 *a, const u8 *b)
{
    return a && b && memcmp(a, b, 6) == 0;
}

static u8 mac_valid(const u8 *mac)
{
    u8 z[6] = {0};
    u8 f[6];

    memset(f, 0xff, 6);
    return mac && !mac_eq(mac, z) && !mac_eq(mac, f);
}

static void contact_persist(void)
{
    syscfg_write(WALKIE_CONTACT_CFG_ID, &s_contacts, sizeof(s_contacts));
}

static int contact_find_mac(const u8 *mac)
{
    int i;

    if (!mac_valid(mac)) {
        return -1;
    }
    for (i = 0; i < s_contacts.n && i < WALKIE_MAX_PEERS; i++) {
        if (mac_eq(s_contacts.items[i].mac, mac)) {
            return i;
        }
    }
    return -1;
}

static void peers_seed_from_contacts(void)
{
    int i, j, free_i;

    os_mutex_pend(&s_peer_lock, 0);
    for (i = 0; i < s_contacts.n && i < WALKIE_MAX_PEERS; i++) {
        const u8 *mac = s_contacts.items[i].mac;
        int found = 0;

        for (j = 0; j < WALKIE_MAX_PEERS; j++) {
            if (s_peers[j].used && mac_valid(s_peers[j].mac) &&
                mac_eq(s_peers[j].mac, mac)) {
                s_peers[j].paired = 1;
                if (s_contacts.items[i].name[0]) {
                    strncpy(s_peers[j].name, s_contacts.items[i].name,
                            WALKIE_PEER_NAME_LEN - 1);
                }
                found = 1;
                break;
            }
        }
        if (found) {
            continue;
        }
        free_i = -1;
        for (j = 0; j < WALKIE_MAX_PEERS; j++) {
            if (!s_peers[j].used) {
                free_i = j;
                break;
            }
        }
        if (free_i < 0) {
            continue;
        }
        memset(&s_peers[free_i], 0, sizeof(s_peers[free_i]));
        s_peers[free_i].used = 1;
        s_peers[free_i].alive = 0;
        s_peers[free_i].paired = 1;
        memcpy(s_peers[free_i].mac, mac, 6);
        strncpy(s_peers[free_i].name, s_contacts.items[i].name,
                WALKIE_PEER_NAME_LEN - 1);
        s_dirty = 1;
    }
    os_mutex_post(&s_peer_lock);
}

void walkie_contact_init(void)
{
    int n;

    if (s_contacts_loaded) {
        return;
    }
    memset(&s_contacts, 0, sizeof(s_contacts));
    n = syscfg_read(WALKIE_CONTACT_CFG_ID, &s_contacts, sizeof(s_contacts));
    if (n != (int)sizeof(s_contacts) || s_contacts.magic != WALKIE_CONTACT_MAGIC ||
        s_contacts.n > WALKIE_MAX_PEERS) {
        memset(&s_contacts, 0, sizeof(s_contacts));
        s_contacts.magic = WALKIE_CONTACT_MAGIC;
    }
    s_contacts_loaded = 1;
    printf("[walkie] contacts loaded n=%u\n", s_contacts.n);
}

int walkie_contact_count(void)
{
    return s_contacts.n;
}

int walkie_contact_info(int idx, u8 mac[6], char *name, int name_len,
                        u8 *alive, u32 *ip)
{
    int i;
    u8 a = 0;
    u32 pip = 0;

    if (idx < 0 || idx >= s_contacts.n || idx >= WALKIE_MAX_PEERS) {
        return -1;
    }
    if (mac) {
        memcpy(mac, s_contacts.items[idx].mac, 6);
    }
    if (name && name_len > 0) {
        strncpy(name, s_contacts.items[idx].name, name_len - 1);
        name[name_len - 1] = '\0';
    }
    os_mutex_pend(&s_peer_lock, 0);
    for (i = 0; i < WALKIE_MAX_PEERS; i++) {
        if (s_peers[i].used && mac_valid(s_peers[i].mac) &&
            mac_eq(s_peers[i].mac, s_contacts.items[idx].mac)) {
            a = s_peers[i].alive;
            pip = s_peers[i].ip;
            break;
        }
    }
    os_mutex_post(&s_peer_lock);
    if (alive) {
        *alive = a;
    }
    if (ip) {
        *ip = pip;
    }
    return 0;
}

u8 walkie_contact_has_mac(const u8 mac[6])
{
    return contact_find_mac(mac) >= 0;
}

int walkie_contact_save_peer(int peer_idx)
{
    const struct walkie_peer *p = walkie_peer_get(peer_idx);
    int ci, i;

    if (!p || !mac_valid(p->mac)) {
        return -1;
    }
    walkie_contact_init();
    ci = contact_find_mac(p->mac);
    if (ci < 0) {
        if (s_contacts.n >= WALKIE_MAX_PEERS) {
            return -1;
        }
        ci = s_contacts.n++;
    }
    memcpy(s_contacts.items[ci].mac, p->mac, 6);
    memset(s_contacts.items[ci].name, 0, sizeof(s_contacts.items[ci].name));
    strncpy(s_contacts.items[ci].name, p->name, WALKIE_PEER_NAME_LEN - 1);
    s_contacts.magic = WALKIE_CONTACT_MAGIC;
    contact_persist();

    os_mutex_pend(&s_peer_lock, 0);
    for (i = 0; i < WALKIE_MAX_PEERS; i++) {
        if (s_peers[i].used && mac_eq(s_peers[i].mac, p->mac)) {
            s_peers[i].paired = 1;
            s_dirty = 1;
            break;
        }
    }
    os_mutex_post(&s_peer_lock);
    printf("[walkie] contact save %s\n", s_contacts.items[ci].name);
    return 0;
}

int walkie_contact_delete(int contact_idx)
{
    u8 mac[6];
    int i;

    if (contact_idx < 0 || contact_idx >= s_contacts.n) {
        return -1;
    }
    memcpy(mac, s_contacts.items[contact_idx].mac, 6);
    if (contact_idx + 1 < s_contacts.n) {
        memmove(&s_contacts.items[contact_idx],
                &s_contacts.items[contact_idx + 1],
                (s_contacts.n - contact_idx - 1) * sizeof(s_contacts.items[0]));
    }
    s_contacts.n--;
    memset(&s_contacts.items[s_contacts.n], 0, sizeof(s_contacts.items[0]));
    contact_persist();

    os_mutex_pend(&s_peer_lock, 0);
    for (i = 0; i < WALKIE_MAX_PEERS; i++) {
        if (!s_peers[i].used || !mac_eq(s_peers[i].mac, mac)) {
            continue;
        }
        s_peers[i].paired = 0;
        if (!s_peers[i].alive) {
            if (s_sel == i) {
                s_sel = -1;
            }
            memset(&s_peers[i], 0, sizeof(s_peers[i]));
        }
        s_dirty = 1;
        break;
    }
    os_mutex_post(&s_peer_lock);
    return 0;
}

static void peers_touch(u32 ip, const char *name, const char *ver, const u8 *mac)
{
    int i, free_i = -1, hit = -1;
    u32 now = walkie_now_ms();
    u8 paired = 0;
    u8 was_alive = 0;

    if (mac_valid(mac) && contact_find_mac(mac) >= 0) {
        paired = 1;
    }

    os_mutex_pend(&s_peer_lock, 0);
    for (i = 0; i < WALKIE_MAX_PEERS; i++) {
        if (!s_peers[i].used) {
            if (free_i < 0) {
                free_i = i;
            }
            continue;
        }
        if (mac_valid(mac) && mac_valid(s_peers[i].mac) &&
            mac_eq(s_peers[i].mac, mac)) {
            hit = i;
            break;
        }
        if ((!mac_valid(mac) || !mac_valid(s_peers[i].mac)) &&
            s_peers[i].ip == ip && ip) {
            hit = i;
            break;
        }
    }
    if (hit >= 0) {
        was_alive = s_peers[hit].alive;
        s_peers[hit].alive = 1;
        s_peers[hit].last_ms = now;
        if (ip && s_peers[hit].ip != ip) {
            s_peers[hit].ip = ip;
            s_dirty = 1;
        }
        if (mac_valid(mac) && !mac_eq(s_peers[hit].mac, mac)) {
            memcpy(s_peers[hit].mac, mac, 6);
            s_dirty = 1;
        }
        if (paired) {
            s_peers[hit].paired = 1;
        }
        if (name && name[0] &&
            strncmp(s_peers[hit].name, name, WALKIE_PEER_NAME_LEN) != 0) {
            strncpy(s_peers[hit].name, name, WALKIE_PEER_NAME_LEN - 1);
            s_dirty = 1;
        }
        if (ver && ver[0]) {
            strncpy(s_peers[hit].ver, ver, WALKIE_VER_LEN - 1);
        }
        if (!was_alive) {
            s_dirty = 1;
        }
        os_mutex_post(&s_peer_lock);
        return;
    }
    if (free_i >= 0) {
        memset(&s_peers[free_i], 0, sizeof(s_peers[free_i]));
        s_peers[free_i].used = 1;
        s_peers[free_i].alive = 1;
        s_peers[free_i].paired = paired;
        s_peers[free_i].ip = ip;
        s_peers[free_i].last_ms = now;
        if (mac_valid(mac)) {
            memcpy(s_peers[free_i].mac, mac, 6);
        }
        if (name) {
            strncpy(s_peers[free_i].name, name, WALKIE_PEER_NAME_LEN - 1);
        }
        if (ver) {
            strncpy(s_peers[free_i].ver, ver, WALKIE_VER_LEN - 1);
        }
        s_dirty = 1;
    }
    os_mutex_post(&s_peer_lock);
}

static void peers_expire(void)
{
    int i;
    u32 now = walkie_now_ms();

    os_mutex_pend(&s_peer_lock, 0);
    for (i = 0; i < WALKIE_MAX_PEERS; i++) {
        if (!s_peers[i].used || !s_peers[i].alive) {
            continue;
        }
        if ((now - s_peers[i].last_ms) <= PEER_TTL_MS) {
            continue;
        }
        if (s_peers[i].paired) {
            /* 已配对: 仅标记离线, 保留条目待再发现 */
            s_peers[i].alive = 0;
            s_dirty = 1;
        } else {
            if (s_sel == i) {
                s_sel = -1;
            }
            memset(&s_peers[i], 0, sizeof(s_peers[i]));
            s_dirty = 1;
        }
    }
    os_mutex_post(&s_peer_lock);
}

static int send_pkt(u32 dip, u8 type, const void *payload, u16 plen)
{
    struct sockaddr_in to;
    u8 buf[8 + 512];
    struct walkie_hdr *h = (struct walkie_hdr *)buf;
    int n;

    if (s_sock < 0 || plen > 512) {
        return -1;
    }
    h->magic = WALKIE_MAGIC;
    h->type = type;
    h->flags = 0;
    h->seq = ++s_seq;
    h->payload_len = plen;
    if (plen && payload) {
        memcpy(buf + sizeof(*h), payload, plen);
    }
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(WALKIE_PORT);
    to.sin_addr.s_addr = dip ? dip : INADDR_BROADCAST;
    n = sendto(s_sock, buf, sizeof(*h) + plen, 0, (struct sockaddr *)&to, sizeof(to));
    return n > 0 ? 0 : -1;
}

static void handle_pkt(u32 sip, const u8 *data, int len)
{
    const struct walkie_hdr *h;
    const u8 *pl;
    u16 plen;

    if (len < (int)sizeof(*h) || sip == 0 || sip == s_self_ip) {
        return;
    }
    h = (const struct walkie_hdr *)data;
    if (h->magic != WALKIE_MAGIC) {
        return;
    }
    plen = h->payload_len;
    if (len < (int)(sizeof(*h) + plen)) {
        return;
    }
    pl = data + sizeof(*h);

    if (h->type == WALKIE_PKT_DISC &&
        plen >= (u16)(WALKIE_VER_LEN + WALKIE_PEER_NAME_LEN)) {
        const struct walkie_disc *d = (const struct walkie_disc *)pl;
        const u8 *mac = NULL;

        /* 同 magic 即认作对讲发现包(不再卡固件/协议小版本, 避免互不可见) */
        if (plen >= sizeof(struct walkie_disc) && mac_valid(d->mac)) {
            mac = d->mac;
            if (mac_eq(mac, s_self_mac)) {
                return;
            }
        }
        peers_touch(sip, d->name, d->ver, mac);
        return;
    }
    if (h->type == WALKIE_PKT_MSG_BEGIN && plen >= sizeof(struct walkie_msg_begin)) {
        walkie_msg_on_begin(sip, (const struct walkie_msg_begin *)pl);
        return;
    }
    if (h->type == WALKIE_PKT_MSG_DATA && plen >= sizeof(struct walkie_msg_chunk)) {
        walkie_msg_on_data(sip, (const struct walkie_msg_chunk *)pl, plen);
        return;
    }
    if (h->type == WALKIE_PKT_MSG_END && plen >= sizeof(struct walkie_msg_end)) {
        walkie_msg_on_end(sip, (const struct walkie_msg_end *)pl);
        return;
    }
    if (h->type == WALKIE_PKT_MSG_ACK && plen >= sizeof(struct walkie_msg_ack)) {
        const struct walkie_msg_ack *a = (const struct walkie_msg_ack *)pl;
        walkie_msg_on_ack(a->msg_id, a->ok);
        return;
    }
    (void)WALKIE_PKT_VOICE;
    (void)WALKIE_PKT_VOICE_END;
}

void walkie_net_refresh_ip(void)
{
    char ip[16];

    Get_IPAddress(WIFI_NETIF, ip);
    s_self_ip = inet_addr(ip);
}

u8 walkie_net_has_ip(void)
{
    walkie_net_refresh_ip();
    return (s_self_ip != 0 && s_self_ip != 0xFFFFFFFFu) ? 1 : 0;
}

static void disc_broadcast(void)
{
    struct walkie_disc d;
    u32 subnet_bc;
    int r1, r2 = -1;

    walkie_net_refresh_ip();
    memset(&d, 0, sizeof(d));
    strncpy(d.ver, WALKIE_PROTO_VER, WALKIE_VER_LEN - 1);
    strncpy(d.name, s_self_name, WALKIE_PEER_NAME_LEN - 1);
    memcpy(d.mac, s_self_mac, 6);

    /* 255.255.255.255 + 同网段广播, 提高跨 AP/STA 发现成功率 */
    r1 = send_pkt(0, WALKIE_PKT_DISC, &d, sizeof(d));
    if (s_self_ip && s_self_ip != 0xFFFFFFFFu) {
        subnet_bc = (s_self_ip & 0x00FFFFFFu) | 0xFF000000u;
        if (subnet_bc != INADDR_BROADCAST) {
            r2 = send_pkt(subnet_bc, WALKIE_PKT_DISC, &d, sizeof(d));
        }
    }
    (void)r1;
    (void)r2;
}

void walkie_net_discover_now(void)
{
    s_force_disc = 1;
}

int walkie_net_scan(void)
{
    if (!walkie_net_has_ip()) {
        printf("[walkie] scan: no IP\n");
        return -2;
    }
    if (walkie_net_start()) {
        printf("[walkie] scan: net not up\n");
        return -1;
    }
    /* 网络任务连续多发; 先发再收, 避免 recv 堵死导致扫不到 */
    s_scan_burst = 10;
    s_force_disc = 1;
    return 0;
}

static void walkie_net_task(void *p)
{
    u8 buf[600];
    struct sockaddr_in from;
    socklen_t flen;
    int n;
    u32 last_disc = 0;
    int sock;

    (void)p;
    while (s_net_run) {
        sock = s_sock;
        if (sock < 0) {
            break;
        }

        /* 先处理扫描/心跳广播, 再 recv — 防止 SO_RCVTIMEO 无效时永远堵死 */
        if (s_scan_burst) {
            s_scan_burst--;
            s_force_disc = 0;
            disc_broadcast();
            last_disc = walkie_now_ms();
        } else if (s_force_disc || (walkie_now_ms() - last_disc) >= DISC_IVAL_MS) {
            s_force_disc = 0;
            disc_broadcast();
            peers_expire();
            last_disc = walkie_now_ms();
        }

        flen = sizeof(from);
        n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
        if (n > 0) {
            handle_pkt(from.sin_addr.s_addr, buf, n);
        }
    }
}

int walkie_net_start(void)
{
    struct sockaddr_in addr;
    int on = 1;
    char ip[16];

    walkie_contact_init();

    if (s_sock >= 0) {
        walkie_net_refresh_ip();
        peers_seed_from_contacts();
        s_force_disc = 1;
        return 0;
    }

    /* 必须等 DHCP/AP 有真实 IP, 再绑 UDP; 0.0.0.0 时绑定会导致之后收发失效 */
    Get_IPAddress(WIFI_NETIF, ip);
    s_self_ip = inet_addr(ip);
    if (!s_self_ip || s_self_ip == 0xFFFFFFFFu) {
        printf("[walkie] net wait IP (now %s)\n", ip);
        return -1;
    }

    if (!s_peer_lock_inited) {
        os_mutex_create(&s_peer_lock);
        s_peer_lock_inited = 1;
    }
    memset(s_peers, 0, sizeof(s_peers));
    s_sel = -1;
    s_dirty = 1;
    s_force_disc = 0;
    s_scan_burst = 0;

    wifi_get_mac(s_self_mac);
    snprintf(s_self_name, sizeof(s_self_name), "WL-%02X%02X%02X",
             s_self_mac[3], s_self_mac[4], s_self_mac[5]);

    s_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sock < 0) {
        printf("[walkie] udp socket fail\n");
        return -1;
    }
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    {
        struct timeval tv = { .tv_sec = 0, .tv_usec = 100 * 1000 };
        setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(WALKIE_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[walkie] udp bind fail\n");
        closesocket(s_sock);
        s_sock = -1;
        return -1;
    }
    s_net_run = 1;
    if (thread_fork("walkie_net", 15, 2048, 0, 0, walkie_net_task, NULL)) {
        printf("[walkie] net task fail\n");
        s_net_run = 0;
        closesocket(s_sock);
        s_sock = -1;
        return -1;
    }
    peers_seed_from_contacts();
    s_force_disc = 1;
    printf("[walkie] net up ip=%s name=%s proto=%s\n",
           ip, s_self_name, WALKIE_PROTO_VER);
    return 0;
}

void walkie_net_stop(void)
{
    int sock;

    s_net_run = 0;
    s_scan_burst = 0;
    sock = s_sock;
    s_sock = -1;
    if (sock >= 0) {
        closesocket(sock);
    }
    /* 等 net task 退出循环, 避免重复 fork */
    os_time_dly(20);
}

int walkie_net_restart(void)
{
    walkie_net_stop();
    return walkie_net_start();
}

int walkie_net_send_msg(u32 dip, u8 type, const void *payload, u16 plen)
{
    if (!dip) {
        return -1;
    }
    return send_pkt(dip, type, payload, plen);
}

int walkie_net_send_voice(const void *pcm, u16 len)
{
    (void)pcm;
    (void)len;
    return -1;
}

void walkie_net_send_voice_end(void)
{
}

int walkie_peer_count(void)
{
    int i, n = 0;

    os_mutex_pend(&s_peer_lock, 0);
    for (i = 0; i < WALKIE_MAX_PEERS; i++) {
        if (s_peers[i].used) {
            n++;
        }
    }
    os_mutex_post(&s_peer_lock);
    return n;
}

const struct walkie_peer *walkie_peer_get(int idx)
{
    int i, n = 0;

    os_mutex_pend(&s_peer_lock, 0);
    for (i = 0; i < WALKIE_MAX_PEERS; i++) {
        if (!s_peers[i].used) {
            continue;
        }
        if (n == idx) {
            os_mutex_post(&s_peer_lock);
            return &s_peers[i];
        }
        n++;
    }
    os_mutex_post(&s_peer_lock);
    return NULL;
}

int walkie_peer_selected(void)
{
    int i, n = 0;

    if (s_sel < 0) {
        return -1;
    }
    os_mutex_pend(&s_peer_lock, 0);
    for (i = 0; i < WALKIE_MAX_PEERS; i++) {
        if (!s_peers[i].used) {
            continue;
        }
        if (i == s_sel) {
            os_mutex_post(&s_peer_lock);
            return n;
        }
        n++;
    }
    os_mutex_post(&s_peer_lock);
    return -1;
}

void walkie_peer_select(int idx)
{
    const struct walkie_peer *p = walkie_peer_get(idx);
    int i, n = 0;

    if (!p) {
        s_sel = -1;
        return;
    }
    os_mutex_pend(&s_peer_lock, 0);
    for (i = 0; i < WALKIE_MAX_PEERS; i++) {
        if (!s_peers[i].used) {
            continue;
        }
        if (n == idx) {
            s_sel = i;
            break;
        }
        n++;
    }
    os_mutex_post(&s_peer_lock);
    s_dirty = 1;
}

u8 walkie_peers_dirty_take(void)
{
    u8 d;

    os_mutex_pend(&s_peer_lock, 0);
    d = s_dirty;
    s_dirty = 0;
    os_mutex_post(&s_peer_lock);
    return d;
}

u32 walkie_peer_selected_ip(void)
{
    u32 ip = 0;

    os_mutex_pend(&s_peer_lock, 0);
    if (s_sel >= 0 && s_sel < WALKIE_MAX_PEERS && s_peers[s_sel].used) {
        ip = s_peers[s_sel].ip;
    }
    os_mutex_post(&s_peer_lock);
    return ip;
}

void walkie_peer_select_ip(u32 ip)
{
    int i;

    if (!ip) {
        s_sel = -1;
        return;
    }
    os_mutex_pend(&s_peer_lock, 0);
    s_sel = -1;
    for (i = 0; i < WALKIE_MAX_PEERS; i++) {
        if (s_peers[i].used && s_peers[i].ip == ip) {
            s_sel = i;
            break;
        }
    }
    os_mutex_post(&s_peer_lock);
}

u8 walkie_peer_ip_alive(u32 ip)
{
    int i;
    u8 ok = 0;

    if (!ip) {
        return 0;
    }
    os_mutex_pend(&s_peer_lock, 0);
    for (i = 0; i < WALKIE_MAX_PEERS; i++) {
        if (s_peers[i].used && s_peers[i].ip == ip && s_peers[i].alive) {
            ok = 1;
            break;
        }
    }
    os_mutex_post(&s_peer_lock);
    return ok;
}

const char *walkie_peer_name_by_ip(u32 ip)
{
    int i;
    static char name[WALKIE_PEER_NAME_LEN];

    name[0] = '\0';
    if (!ip) {
        return name;
    }
    os_mutex_pend(&s_peer_lock, 0);
    for (i = 0; i < WALKIE_MAX_PEERS; i++) {
        if (s_peers[i].used && s_peers[i].ip == ip) {
            strncpy(name, s_peers[i].name, sizeof(name) - 1);
            break;
        }
    }
    os_mutex_post(&s_peer_lock);
    return name;
}
