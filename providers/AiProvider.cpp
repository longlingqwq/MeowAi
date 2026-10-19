#include "providers/AiProvider.h"

AiProvider::AiProvider(QObject *parent)
    : QObject(parent)
{
    // 信号参数为自定义结构：注册后 QueuedConnection 与 QSignalSpy 才能安全搬运
    qRegisterMetaType<ChatError>("ChatError");
    qRegisterMetaType<ChatResponse>("ChatResponse");
    qRegisterMetaType<AiProvider::Protocol>("AiProvider::Protocol");
}
