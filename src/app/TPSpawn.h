/* 启动独立的特权转发核心并发送控制命令。 */
#include "../common/head.h"

@interface TPSpawn : NSObject
/* 按现有 Persona 机制启动特权进程。 */
+ (int)spwn:(NSString *)path host:(NSString *)host port:(NSString *)port;
/* 读取或停止当前转发核心。 */
+ (NSDictionary *)rqst:(NSString *)command;
@end
