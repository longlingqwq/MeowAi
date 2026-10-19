#pragma once

#include "providers/AiProvider.h"

#include <memory>

class NetworkTransport;

/**
 * Provider 工厂：新增 AI 厂商时只需在此登记，上层代码无需改动（拓展接口）。
 *
 * 所有权约定：返回对象由调用方独占（std::unique_ptr），工厂不再返回裸指针。
 * 注意：返回对象**禁止**再调用 setParent()——QObject 父子树与 unique_ptr 会双重 delete。
 */
class ProviderFactory
{
public:
    /// transport 为空时由 Provider 自建；注入时为测试用假实现，所有权归调用方
    /// 失败时返回 nullptr（例如未支持的协议）
    static std::unique_ptr<AiProvider> create(AiProvider::Protocol protocol,
                                              NetworkTransport *transport = nullptr);
};
