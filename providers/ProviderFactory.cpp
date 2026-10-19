#include "providers/ProviderFactory.h"

#include "providers/AnthropicProvider.h"
#include "providers/OpenAiProvider.h"

AiProvider *ProviderFactory::create(AiProvider::Protocol protocol, QObject *parent)
{
    switch (protocol) {
    case AiProvider::OpenAI:
        return new OpenAiProvider(parent);
    case AiProvider::Anthropic:
        return new AnthropicProvider(parent);
    }
    return nullptr;
}
