/* 从 Tunailscale 提取 Persona 启动与独立进程组机制。 */
#include "TPSpawn.h"

@implementation TPSpawn
/* 启动特权转发进程。 */
+ (int)spwn:(NSString *)path host:(NSString *)host port:(NSString *)port {
    posix_spawnattr_t attr;
    int result = posix_spawnattr_init(&attr);
    if (result) return result;
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
    int (*persona)(posix_spawnattr_t *, uid_t, uint32_t) = dlsym(RTLD_DEFAULT, "posix_spawnattr_set_persona_np");
    int (*uid)(posix_spawnattr_t *, uid_t) = dlsym(RTLD_DEFAULT, "posix_spawnattr_set_persona_uid_np");
    int (*gid)(posix_spawnattr_t *, gid_t) = dlsym(RTLD_DEFAULT, "posix_spawnattr_set_persona_gid_np");
    if (!persona || !uid || !gid) { posix_spawnattr_destroy(&attr); return ENOSYS; }
    result = persona(&attr, 99, 1);
    if (!result) result = uid(&attr, 0);
    if (!result) result = gid(&attr, 0);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (!result) result = posix_spawn_file_actions_addopen(&actions, 1, "/tmp/tunproxy.log", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (!result) result = posix_spawn_file_actions_adddup2(&actions, 1, 2);
    char *args[] = {(char *)path.fileSystemRepresentation, (char *)host.UTF8String, (char *)port.UTF8String, NULL};
    char *env[] = {"PATH=/usr/bin:/bin:/usr/sbin:/sbin", "HOME=/var/root", NULL};
    pid_t pid = 0;
    if (!result) result = posix_spawn(&pid, path.fileSystemRepresentation, &actions, &attr, args, env);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (!result) dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        /* 回收本次启动的进程。 */
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
    });
    return result;
}

/* 通过独立 Unix socket 发送固定控制命令。 */
+ (NSDictionary *)rqst:(NSString *)command {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return nil;
    struct timeval timeout = {.tv_sec = 1};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    int noSignal = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal));
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strlcpy(addr.sun_path, "/var/run/tunproxy.sock", sizeof(addr.sun_path));
    addr.sun_len = sizeof(addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return nil; }
    NSData *request = [[command stringByAppendingString:@"\n"] dataUsingEncoding:NSUTF8StringEncoding];
    const char *bytes = request.bytes;
    size_t sent = 0;
    while (sent < request.length) {
        ssize_t count = write(fd, bytes + sent, request.length - sent);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); return nil; }
        sent += count;
    }
    shutdown(fd, SHUT_WR);
    NSMutableData *response = [NSMutableData data];
    char buffer[512];
    ssize_t count;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0) {
        [response appendBytes:buffer length:count];
        if (response.length > 4096) { close(fd); return nil; }
    }
    close(fd);
    id value = [NSJSONSerialization JSONObjectWithData:response options:0 error:NULL];
    return [value isKindOfClass:NSDictionary.class] ? value : nil;
}
@end
