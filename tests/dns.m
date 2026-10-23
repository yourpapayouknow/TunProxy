/* 在独立测试域名下检查真实 DNS 服务注册及自动撤回。 */
#define TP_DNS_CHECK
#include "../src/helper/dns.m"

/* 读取系统当前实际解析器列表。 */
static NSString *dnsview(void) {
    NSTask *task = [NSTask new];
    NSPipe *pipe = [NSPipe pipe];
    task.launchPath = @"/usr/sbin/scutil";
    task.arguments = @[@"--dns"];
    task.standardOutput = pipe;
    [task launch];
    NSData *data = [pipe.fileHandleForReading readDataToEndOfFile];
    [task waitUntilExit];
    return [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
}

/* 注册和移除只匹配测试域名的临时配置。 */
int main(void) {
    @autoreleasepool {
        assert(dnsset("utun5") == 0);
        SCDynamicStoreRef check = SCDynamicStoreCreate(NULL, CFSTR("TunProxy DNS Check"), NULL, NULL);
        CFStringRef key = CFSTR("State:/Network/Service/com.iwmei.tunproxy.test/DNS");
        CFPropertyListRef value = SCDynamicStoreCopyValue(check, key);
        assert(value);
        NSDictionary *config = (__bridge NSDictionary *)value;
        assert([config[@"InterfaceName"] isEqualToString:@"utun5"]);
        assert(([config[@"ServerAddresses"] isEqual:@[@"1.1.1.1", @"8.8.8.8"]]));
        CFRelease(value);
        BOOL visible = NO;
        for (int i = 0; i < 30 && !visible; i++) { usleep(100000); visible = [dnsview() containsString:@"tunproxy-check.invalid"]; }
        assert(visible);
        dnsclr();
        value = NULL;
        for (int i = 0; i < 30; i++) {
            value = SCDynamicStoreCopyValue(check, key);
            if (!value) break;
            CFRelease(value); usleep(100000);
        }
        assert(!value);
        CFRelease(check);
        puts("PASS: isolated supplemental DNS resolver visible in system configuration and removed when session closes");
    }
    return 0;
}
