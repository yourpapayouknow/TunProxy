/* 注册随会话关闭自动移除的系统 DNS 服务。 */
#include "../common/utun_sys.h"

typedef CFTypeRef (*StoreCreate)(CFAllocatorRef, CFStringRef, CFDictionaryRef, void *, void *);
typedef Boolean (*StoreAdd)(CFTypeRef, CFStringRef, CFPropertyListRef);
static CFTypeRef dnsstore;

/* 关闭本进程拥有的临时解析服务。 */
void dnsclr(void) {
    if (dnsstore) { CFRelease(dnsstore); dnsstore = NULL; }
}

/* 将默认域名查询交给经当前 utun 转发的 DNS 地址。 */
int dnsset(const char *interface) {
    @autoreleasepool {
        void *library = dlopen("/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration", RTLD_LAZY);
        StoreCreate create = library ? (StoreCreate)dlsym(library, "SCDynamicStoreCreateWithOptions") : NULL;
        StoreAdd add = library ? (StoreAdd)dlsym(library, "SCDynamicStoreAddTemporaryValue") : NULL;
        int (*error)(void) = library ? dlsym(library, "SCError") : NULL;
        if (!create || !add || !error) { NSLog(@"TunProxy DNS API unavailable"); return -1; }
        dnsstore = create(NULL, CFSTR("TunProxy"), NULL, NULL, NULL);
        if (!dnsstore) { NSLog(@"TunProxy DNS session failed: %d", error()); return -1; }
#ifdef TP_DNS_CHECK
        NSString *domain = @"tunproxy-check.invalid";
        CFStringRef key = CFSTR("State:/Network/Service/com.iwmei.tunproxy.test/DNS");
#else
        NSString *domain = @"";
        CFStringRef key = CFSTR("State:/Network/Service/com.iwmei.tunproxy/DNS");
#endif
        NSDictionary *config = @{
            @"ServerAddresses": @[@"1.1.1.1", @"8.8.8.8"],
            @"InterfaceName": @(interface),
            @"SupplementalMatchDomains": @[domain],
            @"SupplementalMatchOrders": @[@100],
            @"SupplementalMatchDomainsNoSearch": @1
        };
        if (!add(dnsstore, key, (__bridge CFPropertyListRef)config)) {
            NSLog(@"TunProxy DNS registration failed: %d", error()); dnsclr(); return -1;
        }
        NSLog(@"TunProxy DNS registered on %s: 1.1.1.1, 8.8.8.8", interface);
        return 0;
    }
}
