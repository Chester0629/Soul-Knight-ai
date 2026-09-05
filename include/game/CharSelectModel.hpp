#ifndef GAME_CHAR_SELECT_MODEL_HPP
#define GAME_CHAR_SELECT_MODEL_HPP

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Game {

/**
 * @class CharSelectModel
 * @brief Pure, engine-free hero-select state (B5): an ordered roster of character
 *        ids and a cursor that wraps. The CharSelectScene renders it; tests drive
 *        it directly.
 */
class CharSelectModel {
public:
    explicit CharSelectModel(std::vector<std::string> ids, std::size_t start = 0)
        : m_Ids(std::move(ids)), m_Index(m_Ids.empty() ? 0 : start % m_Ids.size()) {}

    std::size_t Count() const { return m_Ids.size(); }
    std::size_t Index() const { return m_Index; }
    const std::string &Current() const { return m_Ids.at(m_Index); }
    const std::string &At(std::size_t i) const { return m_Ids.at(i); }

    void Next() {
        if (!m_Ids.empty()) {
            m_Index = (m_Index + 1) % m_Ids.size();
        }
    }
    void Prev() {
        if (!m_Ids.empty()) {
            m_Index = (m_Index + m_Ids.size() - 1) % m_Ids.size();
        }
    }
    /// Jump to @p id; false (cursor unchanged) if it is not in the roster.
    bool Select(const std::string &id) {
        for (std::size_t i = 0; i < m_Ids.size(); ++i) {
            if (m_Ids[i] == id) {
                m_Index = i;
                return true;
            }
        }
        return false;
    }

private:
    std::vector<std::string> m_Ids;
    std::size_t m_Index = 0;
};

} // namespace Game

#endif /* GAME_CHAR_SELECT_MODEL_HPP */
