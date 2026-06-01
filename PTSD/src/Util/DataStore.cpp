#include "Util/DataStore.hpp"

#include <fstream>
#include <utility>

#include "Util/Logger.hpp"

namespace Util {
const nlohmann::json &DataStore::Load(const std::string &filepath) {
    auto cached = m_Cache.find(filepath);
    if (cached != m_Cache.end()) {
        return cached->second;
    }

    LOG_TRACE("Loading JSON: '{}'", filepath);

    nlohmann::json document = nlohmann::json::object();

    std::ifstream stream(filepath, std::ios::in);
    if (stream.is_open()) {
        // Disable exceptions so malformed input is reported instead of thrown.
        document = nlohmann::json::parse(stream, nullptr, false);
        if (document.is_discarded()) {
            LOG_ERROR("Failed to parse JSON '{}'", filepath);
            document = nlohmann::json::object();
        }
    } else {
        LOG_ERROR("Failed to open JSON '{}'", filepath);
    }

    auto inserted = m_Cache.emplace(filepath, std::move(document));
    return inserted.first->second;
}

bool DataStore::Has(const std::string &filepath) const {
    return m_Cache.find(filepath) != m_Cache.end();
}

void DataStore::Remove(const std::string &filepath) {
    m_Cache.erase(filepath);
}

void DataStore::Clear() {
    m_Cache.clear();
}

std::size_t DataStore::Size() const {
    return m_Cache.size();
}
} // namespace Util
