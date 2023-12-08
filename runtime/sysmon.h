#ifndef NATURE_SYSMON_H
#define NATURE_SYSMON_H

#include "processor.h"

/**
 * - 遍历 share_processor_list 运行时间过长，则发送抢占式信号
 * -
 */
void sysmon_run();

#endif // NATURE_SYSMON_H
