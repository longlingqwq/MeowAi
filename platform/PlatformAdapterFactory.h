#pragma once

class PlatformAdapter;

/**
 * 平台适配器工厂：按编译目标选择实现，上层不感知具体平台。
 */
class PlatformAdapterFactory
{
public:
    /// 当前平台的适配器；进程内单例，随进程退出释放，绝不为空
    static PlatformAdapter *instance();
};
