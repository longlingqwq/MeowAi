#include "providers/ProviderFactory.h"

#include "providers/AnthropicProvider.h"
#include "providers/OpenAiProvider.h"

std::unique_ptr<AiProvider> ProviderFactory::create(AiProvider::Protocol protocol,
                                                    NetworkTransport *transport)
{
    switch (protocol) {
    case AiProvider::OpenAI:
        return std::make_unique<OpenAiProvider>(transport);
    case AiProvider::Anthropic:
        return std::make_unique<AnthropicProvider>(transport);
    }
    return nullptr;
}
