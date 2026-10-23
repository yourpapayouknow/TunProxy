/* 在临时 utun 上检查真实地址配置和文档网段路由。 */
#define main tpmain
#include "../src/helper/proxy.c"
#undef main

/* 添加、查询并撤回单条隔离主机路由。 */
static int rtcheck(int family, unsigned index) {
    const char *host = family == AF_INET ? "192.0.2.77" : "2001:db8::77";
    for (int i = 0; i < 2; i++) {
        unsigned char buffer[512];
        size_t size = rtmsg(buffer, family, 0, i ? RTM_DELETE : RTM_ADD, index);
        size_t addrsize = family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        unsigned offset = family == AF_INET ? 4 : 8;
        inet_pton(family, host, buffer + sizeof(struct rt_msghdr) + offset);
        memset(buffer + sizeof(struct rt_msghdr) + addrsize + sizeof(struct sockaddr_dl) + offset, 255, family == AF_INET ? 4 : 16);
        if (rtxchg(buffer, size)) { perror("isolated kernel route"); return -1; }
        if (!i && rtif(host) != index) { fprintf(stderr, "isolated route has wrong interface\n"); return -1; }
    }
    printf("PASS: kernel IPv%d route add/query/delete\n", family == AF_INET ? 4 : 6);
    return 0;
}

/* 只在临时接口上运行，不添加全局代理路由。 */
int main(void) {
    if (geteuid()) { fprintf(stderr, "Run: sudo ./build/check-kernel\n"); return 2; }
    struct ifaddrs *list;
    if (getifaddrs(&list)) return 3;
    struct in_addr expected;
    inet_pton(AF_INET, "198.19.255.1", &expected);
    for (struct ifaddrs *item = list; item; item = item->ifa_next) {
        if (item->ifa_addr && item->ifa_addr->sa_family == AF_INET &&
            ((struct sockaddr_in *)item->ifa_addr)->sin_addr.s_addr == expected.s_addr) {
            fprintf(stderr, "Test address already exists; refusing changes\n"); freeifaddrs(list); return 4;
        }
    }
    freeifaddrs(list);
    ProbeStepReport report;
    int result = 1;
    tunfd = utnopen(ifname, sizeof(ifname), &report);
    if (tunfd < 0) { fprintf(stderr, "%s: %s\n", report.syscall, report.error_string); return 5; }
    if (utncfgaddr(ifname, "198.19.255.1", "255.255.255.255", "198.19.255.1", &report)) {
        fprintf(stderr, "%s: %s\n", report.syscall, report.error_string); goto cleanup;
    }
    if (cfgv6()) goto cleanup;
    puts("PASS: kernel utun create, IPv4 /32, IPv6 /128 and MTU 1280");
    unsigned index = if_nametoindex(ifname);
    if (!index || rtcheck(AF_INET, index) || rtcheck(AF_INET6, index)) goto cleanup;
    result = 0;
cleanup:
    utnclose(tunfd, ifname);
    if (!result) puts("PASS: temporary utun closed; no global routes changed");
    return result;
}
