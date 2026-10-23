/* 隔离只测试路由与接口的检查程序。 */
#include "../src/common/utun_sys.h"
/* 替代未启动的转发线程。 */
int hev_socks5_tunnel_main_from_str(const unsigned char *config_str, unsigned int config_len, int tun_fd) { return 0; }
/* 替代未启动的转发线程停止操作。 */
void hev_socks5_tunnel_quit(void) {}
/* 隔离 DNS 服务注册。 */
int dnsset(const char *interface) { return 0; }
/* 隔离 DNS 服务撤回。 */
void dnsclr(void) {}
