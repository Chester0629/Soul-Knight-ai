#ifndef UTIL_DATA_STORE_HPP
#define UTIL_DATA_STORE_HPP

#include <cstddef>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace Util {
/**
 * @brief A path-keyed cache over parsed JSON documents.
 *
 * The DataStore class loads JSON documents from disk and caches the parsed
 * result keyed by filepath. It is intended as a lightweight engine data loader
 * for configuration, item definitions, level descriptions, and similar static
 * game data.
 *
 * The store never throws on bad input: a missing file or a malformed document
 * is logged through Util::Logger and cached as an empty JSON object so that the
 * game loop stays alive. Callers can therefore treat @ref Load as
 * always-succeeding and inspect the returned document defensively.
 *
 * Mirrors the caching shape of Util::AssetStore, specialised for
 * `nlohmann::json` payloads.
 */
class DataStore {
public:
    /**
     * @brief Constructs an empty DataStore with no cached documents.
     */
    DataStore() = default;

    /**
     * @brief Parses and caches the JSON document at @p filepath.
     *
     * On the first call for a given path the file is read from disk and parsed.
     * The parsed document is cached and a const reference to it is returned.
     * Subsequent calls with the same path return the cached document directly
     * without touching the disk again, so external changes to the file are not
     * observed until the entry is removed (see @ref Remove) or the cache is
     * cleared (see @ref Clear).
     *
     * This function never throws. If the file cannot be opened or the contents
     * fail to parse, the error is logged through Util::Logger and an empty JSON
     * object (`{}`) is cached and returned for that path.
     *
     * @param filepath The filepath of the JSON document to load.
     * @return A const reference to the cached document for @p filepath.
     */
    const nlohmann::json &Load(const std::string &filepath);

    /**
     * @brief Checks whether a document for @p filepath is currently cached.
     *
     * @param filepath The filepath to query.
     * @return `true` if a document for @p filepath is in the cache, otherwise
     * `false`.
     */
    bool Has(const std::string &filepath) const;

    /**
     * @brief Removes the cached document associated with @p filepath.
     *
     * Does nothing if @p filepath is not currently cached. A following call to
     * @ref Load for the same path will re-read the file from disk.
     *
     * @param filepath The filepath of the document to remove.
     */
    void Remove(const std::string &filepath);

    /**
     * @brief Removes every cached document, emptying the store.
     */
    void Clear();

    /**
     * @brief Returns the number of cached documents.
     *
     * @return The count of entries currently held in the cache.
     */
    std::size_t Size() const;

private:
    std::unordered_map<std::string, nlohmann::json> m_Cache;
};
} // namespace Util

#endif
