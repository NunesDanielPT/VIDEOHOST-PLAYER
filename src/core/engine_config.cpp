#include "vthost/core/engine_config.h"

#include "vthost/core/atomic_file.h"

#include <nlohmann/json.hpp>

namespace vthost {

std::string engineConfigToJson(const EngineConfig& c) {
    nlohmann::json j = {{"gpu", c.gpu}};
    return j.dump(2);
}

EngineConfig engineConfigFromJson(const std::string& text) {
    EngineConfig c;
    try {
        const auto j = nlohmann::json::parse(text);
        if (j.is_object()) {
            auto it = j.find("gpu");
            if (it != j.end() && it->is_string()) c.gpu = it->get<std::string>().substr(0, 256);
        }
    } catch (...) {
    }
    return c;
}

EngineConfig loadEngineConfig(const std::filesystem::path& file) {
    const auto text = readFileLimited(file, 64 * 1024);
    return text ? engineConfigFromJson(*text) : EngineConfig{};
}

bool saveEngineConfig(const std::filesystem::path& file, const EngineConfig& c, std::string* error) {
    return writeFileAtomic(file, engineConfigToJson(c), false, error);
}

} // namespace vthost
