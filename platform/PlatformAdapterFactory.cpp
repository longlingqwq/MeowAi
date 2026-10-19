#include "platform/PlatformAdapterFactory.h"

#include "platform/impl/AndroidPlatformAdapter.h"
#include "platform/impl/DesktopPlatformAdapter.h"
#include "platform/impl/WebPlatformAdapter.h"

#include <memory>

namespace {

std::unique_ptr<PlatformAdapter> createAdapter()
{
#if defined(Q_OS_ANDROID)
    return std::make_unique<AndroidPlatformAdapter>();
#elif defined(Q_OS_WASM)
    return std::make_unique<WebPlatformAdapter>();
#else
    return std::make_unique<DesktopPlatformAdapter>();
#endif
}

} // namespace

PlatformAdapter *PlatformAdapterFactory::instance()
{
    static const std::unique_ptr<PlatformAdapter> adapter = createAdapter();
    return adapter.get();
}
