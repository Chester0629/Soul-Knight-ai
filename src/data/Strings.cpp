#include "data/Strings.hpp"

#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "Util/Logger.hpp"

namespace Game {
namespace {
std::unordered_map<std::string, std::string> g_Map;
bool g_Loaded = false;
} // namespace

void Strings::Load(const std::string &resourceRoot) {
    if (g_Loaded) {
        return;
    }
    g_Loaded = true; // attempt once; a missing file leaves keys passing through.
    std::ifstream f(resourceRoot + "/data/strings.json");
    if (!f) {
        LOG_ERROR("Strings: strings.json not found -- UI stays in key/English form.");
        return;
    }
    try {
        nlohmann::json j;
        f >> j;
        for (auto it = j.begin(); it != j.end(); ++it) {
            g_Map[it.key()] = it.value().get<std::string>();
        }
        LOG_INFO("Strings: loaded {} UI strings.", g_Map.size());
    } catch (const std::exception &e) {
        LOG_ERROR("Strings: parse failed ({}).", e.what());
    }
}

std::string Strings::Get(const std::string &key) {
    const auto it = g_Map.find(key);
    return it != g_Map.end() ? it->second : key;
}

} // namespace Game
