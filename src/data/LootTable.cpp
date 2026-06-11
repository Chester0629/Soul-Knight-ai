#include "data/LootTable.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "Util/DataStore.hpp"
#include "Util/Logger.hpp"
#include "data/RGRandom.hpp"

namespace Game {
namespace {

using nlohmann::json;

// Defensive scalar accessors: wrong/missing/null fields fall back to a default
// instead of throwing, so a partial droptable still loads.
int GetI(const json &j, const char *key, int fallback = 0) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<int>() : fallback;
}

// The drop id is stored under "weapon_id" in the shipped tables; the recreation
// also documents a generic "path" key, so accept either (weapon_id wins).
std::string ReadDropId(const json &e) {
    auto w = e.find("weapon_id");
    if (w != e.end() && w->is_string()) {
        return w->get<std::string>();
    }
    auto p = e.find("path");
    if (p != e.end() && p->is_string()) {
        return p->get<std::string>();
    }
    return std::string();
}

} // namespace

bool LootTable::LoadAll(const std::string &resourceRoot) {
    m_Tiers.clear();

    Util::DataStore store;
    const std::string path = resourceRoot + "/data/droptables.json";
    const json &doc = store.Load(path);

    if (!doc.is_object() || doc.empty()) {
        LOG_ERROR("LootTable: droptables.json is not a non-empty object");
        return false;
    }

    // droptables.json is keyed "0".."6"; parse each key as an int tier level and
    // build a Tier with a precomputed total (== sum of rates == RGChest.total).
    for (auto it = doc.begin(); it != doc.end(); ++it) {
        const json &arr = it.value();
        if (!arr.is_array()) {
            LOG_ERROR("LootTable: tier '{}' is not an array", it.key());
            continue;
        }

        int level = 0;
        try {
            level = std::stoi(it.key());
        } catch (...) {
            LOG_ERROR("LootTable: tier key '{}' is not an integer", it.key());
            continue;
        }

        Tier tier;
        tier.level = level;
        for (const auto &e : arr) {
            if (!e.is_object()) {
                continue;
            }
            Entry entry;
            entry.dropId = ReadDropId(e);
            entry.rate = GetI(e, "rate");
            // Keep zero/negative-rate entries (they never win) so the cumulative
            // walk and total match the original ChestInfo.rates exactly.
            tier.total += entry.rate;
            tier.entries.push_back(std::move(entry));
        }
        m_Tiers.push_back(std::move(tier));
    }

    // Keep tiers in ascending level order so GetTier's nearest-available clamp
    // is a simple ordered scan.
    std::sort(m_Tiers.begin(), m_Tiers.end(),
              [](const Tier &a, const Tier &b) { return a.level < b.level; });

    bool anyUsable = false;
    for (const auto &t : m_Tiers) {
        if (!t.entries.empty() && t.total > 0) {
            anyUsable = true;
            break;
        }
    }
    if (!anyUsable) {
        LOG_ERROR("LootTable: no usable tier loaded from droptables.json");
    }

    LOG_INFO("LootTable loaded: {} tiers from {}", m_Tiers.size(), path);
    return anyUsable;
}

const LootTable::Tier *LootTable::GetTier(int level) const {
    if (m_Tiers.empty()) {
        return nullptr;
    }

    // Exact match, else the nearest available tier: prefer the highest tier
    // whose level <= requested (graceful clamp-down), else the lowest tier.
    const Tier *best = nullptr;
    for (const auto &t : m_Tiers) {
        if (t.level == level) {
            return &t;
        }
        if (t.level <= level && (best == nullptr || t.level > best->level)) {
            best = &t;
        }
    }
    return best != nullptr ? best : &m_Tiers.front();
}

// FAITHFUL: RGChest$$OpenChest @ 0x5AD44C
std::string LootTable::Roll(int tier, RGRandom &rng) const {
    const Tier *t = GetTier(tier);
    if (t == nullptr || t->entries.empty() || t->total <= 0) {
        return std::string();
    }

    // RGRandom.Range(0, total): Unity int semantics, max EXCLUSIVE. Exactly one
    // int draw is consumed per roll (one RGRandom__Range call in the original).
    int r = rng.Range(0, t->total);
    for (const auto &e : t->entries) {
        r -= e.rate;
        if (r < 0) {
            return e.dropId;
        }
    }
    // Numerical fall-through guard: hand back the last entry.
    return t->entries.back().dropId;
}

} // namespace Game
