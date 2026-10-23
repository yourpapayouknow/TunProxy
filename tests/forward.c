/* 使用真实转发入口和隔离的数据报端点检查包转发及退出。 */
#define main tpmain
#include "../src/helper/proxy.c"
#undef main

/* 接收由测试进程继承的数据报描述符。 */
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    tunfd = atoi(argv[1]);
    cfgstr(argv[2], atoi(argv[3]));
    signal(SIGTERM, onsig);
    signal(SIGPIPE, SIG_IGN);
    pthread_t worker;
    if (pthread_create(&worker, NULL, fwd, NULL)) return 3;
    while (!stopping && !atomic_load(&done)) usleep(10000);
    hev_socks5_tunnel_quit();
    pthread_join(worker, NULL);
    return workererr;
}
