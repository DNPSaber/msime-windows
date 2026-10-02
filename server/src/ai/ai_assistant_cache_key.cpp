#include "ai_assistant_cache_key.h"

#include <nlohmann/json.hpp>

namespace AiAssistant::detail
{
std::string BuildSuggestionCacheKey(const Request &request)
{
    return nlohmann::json{{"provider", request.config.provider},
                          {"endpoint", request.config.endpoint},
                          {"model", request.config.model},
                          {"prompt", request.config.prompt},
                          {"pinyin_segments", request.pinyin_segments},
                          {"context", request.context},
                          {"candidate_limit", request.config.candidate_limit}}
        .dump();
}
} // namespace AiAssistant::detail
