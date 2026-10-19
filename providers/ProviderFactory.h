#pragma once

#include "providers/AiProvider.h"

class QObject;

/**
 * Provider 工厂：新增 AI 厂商时只需在此登记，上层代码无需改动（拓展接口）。
 */
class ProviderFactory
{
public:
    /// 失败时返回 nullptr（例如未支持的协议）
    static AiProvider *create(AiProvider::Protocol protocol, QObject *parent = nullptr);
};
