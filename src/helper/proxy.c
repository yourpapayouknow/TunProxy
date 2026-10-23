/* 新 utun 的路由、转发与启停控制。 */
#include "../common/utun_sys.h"
#ifndef TP_CHECK
#include "hev-main.h"
#endif

#define TP_SOCK "/var/run/tunproxy.sock"
#ifndef TP_CHECK
static volatile sig_atomic_t stopping;
static atomic_bool done;
static int tunfd = -1;
static char ifname[PROBE_IFNAME_MAX];
static char config[512];
static int workererr;
#endif

/* 校验尾网地址和端口。 */
static int vlcfg(const char *host, const char *port) {
    struct in_addr a4;
    struct in6_addr a6;
    char *end;
    errno = 0;
    long value = strtol(port, &end, 10);
    if (!*port || *end || errno || value < 1 || value > 65535) return 0;
    if (inet_pton(AF_INET, host, &a4) == 1)
        return (ntohl(a4.s_addr) & 0xffc00000u) == 0x64400000u;
    const unsigned char prefix[] = {0xfd, 0x7a, 0x11, 0x5c, 0xa1, 0xe0};
    return inet_pton(AF_INET6, host, &a6) == 1 && !memcmp(a6.s6_addr, prefix, sizeof(prefix));
}

enum { RT4COUNT = 8, RT6COUNT = 10, RTCOUNT = RT4COUNT + RT6COUNT };

/* 构造非零键捕获路由，保留 Darwin 按零地址回查的物理默认路由。 */
static size_t rtmsg(unsigned char *buffer, int family, int part, int action, unsigned index) {
    memset(buffer, 0, 512);
    struct rt_msghdr *header = (void *)buffer;
    header->rtm_version = RTM_VERSION;
    header->rtm_type = action;
    header->rtm_index = index;
    header->rtm_flags = RTF_UP | RTF_STATIC;
    header->rtm_pid = getpid();
    header->rtm_seq = part + family * 10 + action * 100;
    header->rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
    unsigned char *cursor = buffer + sizeof(*header);
    size_t size = family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    unsigned char dst[sizeof(struct sockaddr_in6)] = {0};
    dst[0] = size;
    dst[1] = family;
    unsigned offset = family == AF_INET ? 4 : 8;
    if (part < 8) dst[offset] = 1u << part;
    else inet_pton(AF_INET6, part == 8 ? "64:ff9b::" : "64:ff9b:1::", dst + offset);
    memcpy(cursor, dst, size);
    cursor += size;
    struct sockaddr_dl gateway = {.sdl_len = sizeof(gateway), .sdl_family = AF_LINK, .sdl_index = index};
    memcpy(cursor, &gateway, sizeof(gateway));
    cursor += sizeof(gateway);
    memset(dst + offset, 0, family == AF_INET ? 4 : 16);
    if (part < 8) dst[offset] = 256u - (1u << part);
    else memset(dst + offset, 255, part == 8 ? 12 : 6);
    memcpy(cursor, dst, size);
    cursor += size;
    header->rtm_msglen = cursor - buffer;
    return header->rtm_msglen;
}

#ifdef TP_CHECK
static unsigned rtif(const char *host);

/* 检查参数、双栈路由报文及只读路由查询。 */
int main(void) {
    unsigned char buffer[512];
    if (!vlcfg("100.64.0.1", "7897") || !vlcfg("fd7a:115c:a1e0::1", "1")) return 1;
    const char *bad[] = {"", "0", "65536", "7897x", "-1"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++) if (vlcfg("100.64.0.1", bad[i])) return 2;
    if (vlcfg("127.0.0.1", "7897") || vlcfg("100.128.0.1", "7897") || vlcfg("host\nport: 1", "7897")) return 3;
    for (int family = AF_INET; family <= AF_INET6; family += AF_INET6 - AF_INET) {
        unsigned matches[256] = {0};
        for (int part = 0; part < 8; part++) {
            size_t size = rtmsg(buffer, family, part, RTM_ADD, 7);
            struct rt_msghdr *header = (void *)buffer;
            unsigned offset = family == AF_INET ? 4 : 8;
            if (header->rtm_msglen != size || header->rtm_index != 7) return 4;
            if (!buffer[sizeof(*header) + offset]) return 8;
            if (buffer[sizeof(*header) + offset] != (1u << part)) return 5;
            size_t addrsize = family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
            if (buffer[sizeof(*header) + addrsize + sizeof(struct sockaddr_dl) + offset] != 256u - (1u << part)) return 6;
            unsigned char *dst = buffer + sizeof(*header) + offset;
            unsigned char *mask = buffer + sizeof(*header) + addrsize + sizeof(struct sockaddr_dl) + offset;
            for (unsigned byte = 0; byte < 256; byte++) if ((byte & *mask) == *dst) matches[byte]++;
            for (unsigned byte = 1; byte < (family == AF_INET ? 4u : 16u); byte++) if (dst[byte] || mask[byte]) return 9;
        }
        if (matches[0]) return 10;
        for (int byte = 1; byte < 256; byte++) if (matches[byte] != 1) return 11;
    }
    for (int part = 8; part < RT6COUNT; part++) {
        rtmsg(buffer, AF_INET6, part, RTM_ADD, 7);
        unsigned char expected[16];
        inet_pton(AF_INET6, part == 8 ? "64:ff9b::" : "64:ff9b:1::", expected);
        if (memcmp(buffer + sizeof(struct rt_msghdr) + 8, expected, 16)) return 12;
        unsigned char *mask = buffer + sizeof(struct rt_msghdr) + sizeof(struct sockaddr_in6) + sizeof(struct sockaddr_dl) + 8;
        for (int byte = 0; byte < 16; byte++) if (mask[byte] != (byte < (part == 8 ? 12 : 6) ? 255 : 0)) return 13;
    }
    unsigned loopback = if_nametoindex("lo0");
    if (!loopback || rtif("127.0.0.1") != loopback || rtif("::1") != loopback) return 7;
    puts("TunProxy parameters, non-zero IPv4/IPv6 and NAT64 route coverage, and kernel query checks passed");
    return 0;
}
#endif
/* 与内核交换路由消息并检查操作结果。 */
static int rtxchg(unsigned char *buffer, size_t size) {
    int fd = socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC);
    if (fd < 0) return -1;
    struct timeval timeout = {.tv_sec = 2};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
        int saved = errno; close(fd); errno = saved; return -1;
    }
    int sequence = ((struct rt_msghdr *)buffer)->rtm_seq;
    int action = ((struct rt_msghdr *)buffer)->rtm_type;
    ssize_t sent;
    do { sent = write(fd, buffer, size); } while (sent < 0 && errno == EINTR);
    if (sent != (ssize_t)size) {
        int saved = sent < 0 ? errno : EIO; close(fd); errno = saved; return -1;
    }
    ssize_t count;
    int result = -1;
    for (;;) {
        count = read(fd, buffer, 512);
        if (count < 0) { if (errno == EINTR) continue; break; }
        if (!count) { errno = ECONNRESET; break; }
        if (count < (ssize_t)sizeof(struct rt_msghdr)) continue;
        struct rt_msghdr *reply = (void *)buffer;
        if (reply->rtm_version != RTM_VERSION || reply->rtm_type != action ||
            reply->rtm_pid != getpid() || reply->rtm_seq != sequence) continue;
        if (reply->rtm_msglen < sizeof(*reply) || reply->rtm_msglen > count) { errno = EMSGSIZE; break; }
        errno = reply->rtm_errno;
        result = errno ? -1 : 0;
        break;
    }
    int saved = errno;
    close(fd);
    errno = saved;
    return result;
}

#ifndef TP_CHECK
/* 添加或撤回本接口的路由。 */
static int rtset(unsigned route, int action, unsigned index) {
    unsigned char buffer[512];
    int family = route < RT4COUNT ? AF_INET : AF_INET6;
    int part = route < RT4COUNT ? route : route - RT4COUNT;
    size_t size = rtmsg(buffer, family, part, action, index);
    int result = rtxchg(buffer, size);
    if (result) fprintf(stderr, "route %s IPv%d part %d on interface %u: %s (errno=%d)\n",
        action == RTM_ADD ? "add" : "delete", family == AF_INET ? 4 : 6, part, index, strerror(errno), errno);
    return result;
}
#endif

/* 查询上游地址当前使用的接口。 */
static unsigned rtif(const char *host) {
    unsigned char buffer[512] = {0};
    struct rt_msghdr *header = (void *)buffer;
    header->rtm_version = RTM_VERSION;
    header->rtm_type = RTM_GET;
    header->rtm_pid = getpid();
    header->rtm_seq = 799;
    header->rtm_addrs = RTA_DST;
    struct sockaddr_in *a4 = (void *)(buffer + sizeof(*header));
    if (inet_pton(AF_INET, host, &a4->sin_addr) == 1) {
        a4->sin_len = sizeof(*a4);
        a4->sin_family = AF_INET;
        header->rtm_msglen = sizeof(*header) + sizeof(*a4);
    } else {
        struct sockaddr_in6 *a6 = (void *)a4;
        a6->sin6_len = sizeof(*a6);
        a6->sin6_family = AF_INET6;
        inet_pton(AF_INET6, host, &a6->sin6_addr);
        header->rtm_msglen = sizeof(*header) + sizeof(*a6);
    }
    return rtxchg(buffer, header->rtm_msglen) ? 0 : header->rtm_index;
}

#ifndef TP_CHECK
/* 确认该接口持有尾网地址。 */
static int tailif(unsigned index) {
    struct ifaddrs *list;
    if (!index || getifaddrs(&list)) return 0;
    int found = 0;
    for (struct ifaddrs *item = list; item; item = item->ifa_next) {
        if (!item->ifa_addr || !(item->ifa_flags & IFF_UP) || if_nametoindex(item->ifa_name) != index) continue;
        char host[INET6_ADDRSTRLEN];
        int family = item->ifa_addr->sa_family;
        const void *addr = family == AF_INET ? (void *)&((struct sockaddr_in *)item->ifa_addr)->sin_addr :
            family == AF_INET6 ? (void *)&((struct sockaddr_in6 *)item->ifa_addr)->sin6_addr : NULL;
        if (addr && inet_ntop(family, addr, host, sizeof(host)) && vlcfg(host, "1")) { found = 1; break; }
    }
    freeifaddrs(list);
    return found;
}

/* 复用原项目的点对点 /128 配置并设置 MTU。 */
static int cfgv6(void) {
    struct tp_alias6 request = {0};
    strlcpy(request.name, ifname, sizeof(request.name));
    request.addr.sin6_len = request.mask.sin6_len = sizeof(struct sockaddr_in6);
    request.addr.sin6_family = request.mask.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "fd00:7072:6f78::1", &request.addr.sin6_addr);
    request.dst = request.addr;
    memset(&request.mask.sin6_addr, 255, 16);
    request.lifetime.valid = request.lifetime.preferred_lifetime = UINT32_MAX;
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0) { perror("IPv6 control socket"); return -1; }
    int result = ioctl(fd, _IOW('i', 26, struct tp_alias6), &request);
    if (result) perror("IPv6 SIOCAIFADDR_IN6");
    struct ifreq mtu = {.ifr_mtu = 1280};
    strlcpy(mtu.ifr_name, ifname, sizeof(mtu.ifr_name));
    if (!result) {
        result = ioctl(fd, SIOCSIFMTU, &mtu);
        if (result) perror("MTU SIOCSIFMTU");
    }
    int saved = errno;
    close(fd);
    errno = saved;
    return result;
}

/* 标记停止请求。 */
static void onsig(int number) { stopping = 1; }

/* 生成独立转发核心使用的配置。 */
static void cfgstr(const char *host, int port) {
    snprintf(config, sizeof(config), "tunnel:\n  mtu: 1280\nsocks5:\n  address: '%s'\n  port: %d\n  udp: udp\nmisc:\n  log-level: warn\n  max-session-count: 2048\n", host, port);
}

/* 将现有 utun 文件描述符交给上游转发核心。 */
static void *fwd(void *unused) {
    workererr = hev_socks5_tunnel_main_from_str((unsigned char *)config, strlen(config), tunfd);
    atomic_store(&done, 1);
    return NULL;
}

/* 返回当前核心状态或接受停止命令。 */
static void ctl(int server, const char *host, int port) {
    int fd = accept(server, NULL, NULL);
    if (fd < 0) return;
    uid_t uid;
    gid_t gid;
    struct timeval timeout = {.tv_sec = 1};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (getpeereid(fd, &uid, &gid) || (uid != 0 && uid != 501)) { close(fd); return; }
    char command[16] = {0};
    size_t used = 0;
    while (used < sizeof(command) - 1) {
        ssize_t count = read(fd, command + used, sizeof(command) - used - 1);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += count;
        if (memchr(command, '\n', used)) break;
    }
    if (used && (!strcmp(command, "status\n") || !strcmp(command, "stop\n"))) {
        if (!strcmp(command, "stop\n")) stopping = 1;
        char response[256];
        int length = snprintf(response, sizeof(response), "{\"running\":%s,\"pid\":%d,\"interface\":\"%s\",\"host\":\"%s\",\"port\":%d}\n", stopping || atomic_load(&done) ? "false" : "true", getpid(), ifname, host, port);
        send(fd, response, length, 0);
    }
    close(fd);
}

/* 创建独立 utun 并在停止时撤回本次添加的路由。 */
int main(int argc, char **argv) {
    if (argc != 3 || !vlcfg(argv[1], argv[2])) { fprintf(stderr, "Invalid Tailscale IP or port\n"); return 2; }
    if (geteuid() != 0) { fprintf(stderr, "Root helper required\n"); return 3; }
    unsigned tailindex = rtif(argv[1]);
    if (!tailindex) { perror("Tailscale upstream route lookup"); return 6; }
    if (!tailif(tailindex)) { fprintf(stderr, "Connect Tunailscale before enabling this proxy\n"); return 6; }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, onsig);
    signal(SIGINT, onsig);
    int lock = open("/var/run/tunproxy.lock", O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) { perror("TunProxy already running"); return 4; }
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un address = {.sun_len = sizeof(address), .sun_family = AF_UNIX};
    strlcpy(address.sun_path, TP_SOCK, sizeof(address.sun_path));
    unlink(TP_SOCK);
    if (server < 0 || bind(server, (void *)&address, sizeof(address)) || chmod(TP_SOCK, 0666) || listen(server, 4)) { perror("control socket"); return 5; }
    ProbeStepReport report;
    unsigned installed = 0;
    unsigned index = 0;
    int result = 1;
    pthread_t worker;
    int started = 0;
    tunfd = utnopen(ifname, sizeof(ifname), &report);
    if (tunfd < 0) {
        fprintf(stderr, "utun create %s: %s (errno=%d)\n", report.syscall, report.error_string, report.err_no);
        goto cleanup;
    }
    if (utncfgaddr(ifname, "198.19.255.1", "255.255.255.255", "198.19.255.1", &report)) {
        fprintf(stderr, "IPv4 %s: %s (errno=%d)\n", report.syscall, report.error_string, report.err_no);
        goto cleanup;
    }
    if (cfgv6()) goto cleanup;
    index = if_nametoindex(ifname);
    if (!index) { perror("utun interface index"); goto cleanup; }
    cfgstr(argv[1], atoi(argv[2]));
    int threaderr = pthread_create(&worker, NULL, fwd, NULL);
    if (threaderr) { fprintf(stderr, "forwarder thread: %s\n", strerror(threaderr)); goto cleanup; }
    started = 1;
    for (unsigned i = 0; i < RTCOUNT; i++) {
        if (rtset(i, RTM_ADD, index)) goto cleanup;
        installed++;
    }
    if (dnsset(ifname)) goto cleanup;
    printf("TunProxy %s -> %s:%s\n", ifname, argv[1], argv[2]);
    fflush(stdout);
    while (!stopping && !atomic_load(&done)) {
        struct pollfd watch = {.fd = server, .events = POLLIN};
        if (poll(&watch, 1, 250) > 0 && (watch.revents & POLLIN)) ctl(server, argv[1], atoi(argv[2]));
        if (rtif(argv[1]) != tailindex) { fprintf(stderr, "Tailscale route changed; stopping proxy\n"); break; }
    }
    result = stopping ? 0 : 1;
cleanup:
    dnsclr();
    while (installed) {
        unsigned i = --installed;
        rtset(i, RTM_DELETE, index);
    }
    if (started) {
        hev_socks5_tunnel_quit();
        pthread_join(worker, NULL);
        if (workererr) fprintf(stderr, "forwarder exited: %d\n", workererr);
    }
    if (tunfd >= 0) utnclose(tunfd, ifname);
    close(server);
    unlink(TP_SOCK);
    close(lock);
    return result;
}
#endif
