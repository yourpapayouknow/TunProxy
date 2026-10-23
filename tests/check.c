/* 检查真实配置函数的 Darwin ABI、内核约束和错误传播。 */
#define TP_TEST
#include "../src/common/head.h"
#undef ifra_dstaddr

static int tpioctl(int fd, unsigned long op, ...);
static int tpclose(int fd);
static ssize_t tpread(int fd, void *buffer, size_t size);
static ssize_t tpwrite(int fd, const void *buffer, size_t size);
static int tpsocket(int domain, int type, int protocol);
static int tpsockopt(int fd, int level, int option, const void *value, socklen_t size);
static int tppeer(int fd, uid_t *uid, gid_t *gid);

#define ioctl tpioctl
#define close tpclose
#define read tpread
#define write tpwrite
#define socket tpsocket
#define setsockopt tpsockopt
#define getpeereid tppeer
#define main tpmain
#include "../src/helper/proxy.c"
#include "../src/common/utun_sys.c"
#undef main
#undef setsockopt
#undef getpeereid
#undef socket
#undef write
#undef read
#undef close
#undef ioctl

_Static_assert(sizeof(struct tp_alias6) == sizeof(struct in6_aliasreq), "IPv6 ABI size");
_Static_assert(offsetof(struct tp_alias6, lifetime) == offsetof(struct in6_aliasreq, ifra_lifetime), "IPv6 ABI offset");
_Static_assert(_IOW('i', 26, struct tp_alias6) == SIOCAIFADDR_IN6, "IPv6 ioctl ABI");

static unsigned long failed;
static unsigned calls;
static int notices, reads, routeerr;
static int readerr, truncated;
static unsigned char request[512];

/* 截获地址和接口配置请求，执行 Darwin 点对点校验。 */
static int tpioctl(int fd, unsigned long op, ...) {
    (void)fd;
    va_list args;
    va_start(args, op);
    void *data = va_arg(args, void *);
    va_end(args);
    calls++;
    if (op == SIOCAIFADDR_IN6) {
        struct in6_aliasreq *req = data;
        assert(!strcmp(req->ifra_name, "utun99"));
        assert(req->ifra_addr.sin6_family == AF_INET6);
        assert(req->ifra_addr.sin6_len == sizeof(struct sockaddr_in6));
        if (req->ifra_dstaddr.sin6_family == AF_INET6) {
            for (int i = 0; i < 16; i++) {
                if (req->ifra_prefixmask.sin6_addr.s6_addr[i] != 255) { errno = EINVAL; return -1; }
            }
        }
        assert(!memcmp(&req->ifra_addr, &req->ifra_dstaddr, sizeof(req->ifra_addr)));
        assert(req->ifra_lifetime.ia6t_vltime == UINT32_MAX);
    } else if (op == SIOCSIFMTU) {
        assert(((struct ifreq *)data)->ifr_mtu == 1280);
    } else if (op == SIOCGIFFLAGS) {
        ((struct ifreq *)data)->ifr_flags = IFF_POINTOPOINT;
    } else if (op == SIOCSIFFLAGS) {
        assert(((struct ifreq *)data)->ifr_flags == (IFF_UP | IFF_POINTOPOINT));
    } else if (op == SIOCGIFADDR) {
        struct sockaddr_in *addr = (void *)&((struct ifreq *)data)->ifr_addr;
        addr->sin_family = AF_INET;
        inet_pton(AF_INET, "198.19.255.1", &addr->sin_addr);
    } else {
        assert(op == SIOCAIFADDR);
    }
    if (op == failed) { errno = EPERM; return -1; }
    return 0;
}
/* 模拟关闭期间 errno 被其他系统调用覆盖。 */
static int tpclose(int fd) {
    int result = fd == 80 ? 0 : close(fd);
    errno = EBADF;
    return result;
}

/* 隔离路由修改，其他套接字仍使用系统实现。 */
static int tpsocket(int domain, int type, int protocol) {
    return domain == PF_ROUTE ? 80 : socket(domain, type, protocol);
}

/* 接受路由套接字的超时设置。 */
static int tpsockopt(int fd, int level, int option, const void *value, socklen_t size) {
    return fd == 80 ? 0 : setsockopt(fd, level, option, value, size);
}

/* 保存真实路由请求供回复匹配。 */
static ssize_t tpwrite(int fd, const void *buffer, size_t size) {
    if (fd != 80) return write(fd, buffer, size);
    assert(fd == 80 && size <= sizeof(request));
    memcpy(request, buffer, size);
    reads = 0;
    return size;
}

/* 注入无关通知、信号中断和内核错误回复。 */
static ssize_t tpread(int fd, void *buffer, size_t size) {
    if (fd != 80) return read(fd, buffer, size);
    assert(fd == 80 && size >= sizeof(request));
    if (readerr) { errno = readerr; return -1; }
    reads++;
    if (notices && reads == 1) { memset(buffer, 0, 8); return 8; }
    if (notices && reads == 2) { errno = EINTR; return -1; }
    memcpy(buffer, request, sizeof(request));
    struct rt_msghdr *reply = buffer;
    reply->rtm_msglen = truncated ? 512 : sizeof(*reply);
    reply->rtm_index = 7;
    reply->rtm_errno = routeerr;
    if (notices && reads == 3) reply->rtm_pid++;
    return sizeof(*reply);
}

/* 为本地控制检查提供被允许的对端身份。 */
static int tppeer(int fd, uid_t *uid, gid_t *gid) {
    (void)fd; *uid = 501; *gid = 501; return 0;
}

/* 检查运行、退出和停止命令返回的真实状态。 */
static void ctlcheck(void) {
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {.sun_len = sizeof(addr), .sun_family = AF_UNIX};
    snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/tunproxy-check-%d.sock", getpid());
    assert(server >= 0 && !bind(server, (void *)&addr, sizeof(addr)) && !listen(server, 4));
    for (int state = 0; state < 3; state++) {
        atomic_store(&done, state == 1);
        int client = socket(AF_UNIX, SOCK_STREAM, 0);
        assert(client >= 0 && !connect(client, (void *)&addr, sizeof(addr)));
        const char *command = state == 2 ? "stop\n" : "status\n";
        assert(write(client, command, strlen(command)) == (ssize_t)strlen(command));
        ctl(server, "100.64.0.1", 7897);
        char response[256] = {0};
        assert(read(client, response, sizeof(response) - 1) > 0);
        assert(strstr(response, state == 0 ? "\"running\":true" : "\"running\":false"));
        close(client);
    }
    assert(stopping);
    close(server);
    unlink(addr.sun_path);
}

/* 使用相同入口检查配置成功、拒绝、通知和路由撤回。 */
int main(void) {
    strlcpy(ifname, "utun99", sizeof(ifname));
    assert(cfgv6() == 0 && calls == 2);
    failed = SIOCAIFADDR_IN6;
    assert(cfgv6() == -1 && errno == EPERM);
    failed = SIOCSIFMTU;
    assert(cfgv6() == -1 && errno == EPERM);
    failed = 0;
    ProbeStepReport report;
    assert(utncfgaddr(ifname, "198.19.255.1", "255.255.255.255", "198.19.255.1", &report) == 0);
    failed = SIOCSIFFLAGS;
    assert(utncfgaddr(ifname, "198.19.255.1", "255.255.255.255", "198.19.255.1", &report) == -1);
    assert(report.err_no == EPERM);
    failed = SIOCGIFFLAGS;
    assert(utncfgaddr(ifname, "198.19.255.1", "255.255.255.255", "198.19.255.1", &report) == -1);
    assert(report.err_no == EPERM);
    failed = 0;
    notices = 1;
    assert(rtif("100.64.0.1") == 7 && reads == 4);
    assert(rtif("fd7a:115c:a1e0::1") == 7 && reads == 4);
    notices = 0;
    routeerr = EEXIST;
    assert(rtset(0, RTM_ADD, 7) == -1 && errno == EEXIST);
    routeerr = 0;
    for (unsigned i = 0; i < RTCOUNT; i++) {
        assert(rtset(i, RTM_ADD, 7) == 0);
        unsigned offset = i < RT4COUNT ? 4 : 8;
        unsigned width = i < RT4COUNT ? 4 : 16;
        unsigned char zero[16] = {0};
        assert(memcmp(request + sizeof(struct rt_msghdr) + offset, zero, width));
    }
    for (unsigned i = RTCOUNT; i > 0; i--) assert(rtset(i - 1, RTM_DELETE, 7) == 0);
    readerr = EAGAIN;
    assert(rtif("100.64.0.1") == 0 && errno == EAGAIN);
    readerr = 0;
    truncated = 1;
    assert(rtif("100.64.0.1") == 0 && errno == EMSGSIZE);
    truncated = 0;
    ctlcheck();
    puts("PASS: Darwin address ABI, point-to-point IPv6, interface flags, error propagation, route notifications and control status");
    return 0;
}
