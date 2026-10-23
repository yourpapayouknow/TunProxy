/*
 * utun_sys.c - Darwin 原生 utun 控制与路由系统调用实现
 */

#include "utun_sys.h"

/* 创建原生utun设备并保留fd */
int utnopen(char *out_ifname, size_t max_len, ProbeStepReport *rpt) {
    if (!out_ifname || max_len < PROBE_IFNAME_MAX || !rpt) {
        return -1;
    }
    memset(rpt, 0, sizeof(*rpt));
    strncpy(rpt->test, "P2-utun-create", sizeof(rpt->test) - 1);
    prbgetiso(rpt->timestamp, sizeof(rpt->timestamp));
    rpt->pid = getpid();
    rpt->uid = getuid();
    rpt->euid = geteuid();

    int fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0) {
        strncpy(rpt->result, "FAIL", sizeof(rpt->result) - 1);
        strncpy(rpt->syscall, "socket(PF_SYSTEM)", sizeof(rpt->syscall) - 1);
        rpt->return_value = -1;
        rpt->err_no = errno;
        strerror_r(errno, rpt->error_string, sizeof(rpt->error_string));
        snprintf(rpt->details, sizeof(rpt->details), "Failed to open PF_SYSTEM socket");
        return -1;
    }

    struct ctl_info ctl_info;
    memset(&ctl_info, 0, sizeof(ctl_info));
    strncpy(ctl_info.ctl_name, UTUN_CONTROL_NAME, sizeof(ctl_info.ctl_name) - 1);

    if (ioctl(fd, CTLIOCGINFO, &ctl_info) < 0) {
        strncpy(rpt->result, "FAIL", sizeof(rpt->result) - 1);
        strncpy(rpt->syscall, "ioctl(CTLIOCGINFO)", sizeof(rpt->syscall) - 1);
        rpt->return_value = -1;
        rpt->err_no = errno;
        strerror_r(errno, rpt->error_string, sizeof(rpt->error_string));
        snprintf(rpt->details, sizeof(rpt->details), "Failed to query utun control ID");
        close(fd);
        return -1;
    }

    struct sockaddr_ctl sc;
    memset(&sc, 0, sizeof(sc));
    sc.sc_len = sizeof(sc);
    sc.sc_family = AF_SYSTEM;
    sc.ss_sysaddr = AF_SYS_CONTROL;
    sc.sc_id = ctl_info.ctl_id;
    sc.sc_unit = 0; /* 0 表示请求内核自动分配空闲 unit 编号 */

    if (connect(fd, (struct sockaddr *)&sc, sizeof(sc)) < 0) {
        strncpy(rpt->result, "FAIL", sizeof(rpt->result) - 1);
        strncpy(rpt->syscall, "connect(sockaddr_ctl)", sizeof(rpt->syscall) - 1);
        rpt->return_value = -1;
        rpt->err_no = errno;
        strerror_r(errno, rpt->error_string, sizeof(rpt->error_string));
        snprintf(rpt->details, sizeof(rpt->details), "Kernel rejected utun connect");
        close(fd);
        return -1;
    }

    socklen_t ifname_len = (socklen_t)max_len;
    if (getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, out_ifname, &ifname_len) < 0) {
        strncpy(rpt->result, "FAIL", sizeof(rpt->result) - 1);
        strncpy(rpt->syscall, "getsockopt(UTUN_OPT_IFNAME)", sizeof(rpt->syscall) - 1);
        rpt->return_value = -1;
        rpt->err_no = errno;
        strerror_r(errno, rpt->error_string, sizeof(rpt->error_string));
        snprintf(rpt->details, sizeof(rpt->details), "Failed to get allocated utun interface name");
        close(fd);
        return -1;
    }

    unsigned int if_idx = if_nametoindex(out_ifname);
    strncpy(rpt->ifname, out_ifname, sizeof(rpt->ifname) - 1);
    strncpy(rpt->result, "PASS", sizeof(rpt->result) - 1);
    strncpy(rpt->syscall, "connect", sizeof(rpt->syscall) - 1);
    rpt->return_value = 0;
    rpt->err_no = 0;
    snprintf(rpt->details, sizeof(rpt->details), "utun interface %s allocated, if_index=%u, fd=%d", out_ifname, if_idx, fd);

    return fd;
}

/* 安全关闭utun设备 */
int utnclose(int fd, const char *ifname) {
    if (fd >= 0) {
        close(fd);
    }
    return 0;
}

/* 配置接口IPv4地址与掩码并启动接口 */
int utncfgaddr(const char *ifname, const char *ip_str, const char *mask_str, const char *dst_str, ProbeStepReport *rpt) {
    if (!ifname || !ip_str || !mask_str || !dst_str || !rpt) {
        return -1;
    }
    memset(rpt, 0, sizeof(*rpt));
    strncpy(rpt->test, "P3-config-addr", sizeof(rpt->test) - 1);
    prbgetiso(rpt->timestamp, sizeof(rpt->timestamp));
    rpt->pid = getpid();
    rpt->uid = getuid();
    rpt->euid = geteuid();
    strncpy(rpt->ifname, ifname, sizeof(rpt->ifname) - 1);

    int ctl_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctl_fd < 0) {
        strncpy(rpt->result, "FAIL", sizeof(rpt->result) - 1);
        strncpy(rpt->syscall, "socket(AF_INET)", sizeof(rpt->syscall) - 1);
        rpt->return_value = -1;
        rpt->err_no = errno;
        strerror_r(errno, rpt->error_string, sizeof(rpt->error_string));
        snprintf(rpt->details, sizeof(rpt->details), "Failed to open AF_INET control socket");
        return -1;
    }

    struct in_aliasreq ifra;
    memset(&ifra, 0, sizeof(ifra));
    strncpy(ifra.ifra_name, ifname, sizeof(ifra.ifra_name) - 1);

    ifra.ifra_addr.sin_len = sizeof(struct sockaddr_in);
    ifra.ifra_addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip_str, &ifra.ifra_addr.sin_addr);

    ifra.ifra_mask.sin_len = sizeof(struct sockaddr_in);
    ifra.ifra_mask.sin_family = AF_INET;
    inet_pton(AF_INET, mask_str, &ifra.ifra_mask.sin_addr);

    ifra.ifra_broadaddr.sin_len = sizeof(struct sockaddr_in);
    ifra.ifra_broadaddr.sin_family = AF_INET;
    inet_pton(AF_INET, dst_str, &ifra.ifra_broadaddr.sin_addr);

    if (ioctl(ctl_fd, SIOCAIFADDR, &ifra) < 0) {
        strncpy(rpt->result, "FAIL", sizeof(rpt->result) - 1);
        strncpy(rpt->syscall, "ioctl(SIOCAIFADDR)", sizeof(rpt->syscall) - 1);
        rpt->return_value = -1;
        rpt->err_no = errno;
        strerror_r(errno, rpt->error_string, sizeof(rpt->error_string));
        snprintf(rpt->details, sizeof(rpt->details), "SIOCAIFADDR rejected for %s with IP %s", ifname, ip_str);
        close(ctl_fd);
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
    const char *operation = "ioctl(SIOCGIFFLAGS)";
    int up = ioctl(ctl_fd, SIOCGIFFLAGS, &ifr);
    if (up >= 0) {
        ifr.ifr_flags |= IFF_UP;
        operation = "ioctl(SIOCSIFFLAGS)";
        up = ioctl(ctl_fd, SIOCSIFFLAGS, &ifr);
    }
    if (up < 0) {
        strncpy(rpt->result, "FAIL", sizeof(rpt->result) - 1);
        strncpy(rpt->syscall, operation, sizeof(rpt->syscall) - 1);
        rpt->return_value = -1;
        rpt->err_no = errno;
        strerror_r(errno, rpt->error_string, sizeof(rpt->error_string));
        snprintf(rpt->details, sizeof(rpt->details), "Failed to bring up %s", ifname);
        close(ctl_fd);
        errno = rpt->err_no;
        return -1;
    }

    /* 验证读取配置 */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
    if (ioctl(ctl_fd, SIOCGIFADDR, &ifr) < 0) {
        strncpy(rpt->result, "FAIL", sizeof(rpt->result) - 1);
        strncpy(rpt->syscall, "ioctl(SIOCGIFADDR)", sizeof(rpt->syscall) - 1);
        rpt->return_value = -1;
        rpt->err_no = errno;
        strerror_r(errno, rpt->error_string, sizeof(rpt->error_string));
        snprintf(rpt->details, sizeof(rpt->details), "Failed to readback IP from %s", ifname);
        close(ctl_fd);
        return -1;
    }

    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
    char readback_ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &sin->sin_addr, readback_ip, sizeof(readback_ip));

    close(ctl_fd);

    strncpy(rpt->result, "PASS", sizeof(rpt->result) - 1);
    strncpy(rpt->syscall, "ioctl(SIOCAIFADDR)", sizeof(rpt->syscall) - 1);
    rpt->return_value = 0;
    rpt->err_no = 0;
    snprintf(rpt->details, sizeof(rpt->details), "IP %s set & readback %s on %s successfully", ip_str, readback_ip, ifname);
    return 0;
}

