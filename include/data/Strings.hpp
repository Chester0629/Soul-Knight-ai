#ifndef GAME_STRINGS_HPP
#define GAME_STRINGS_HPP

#include <string>

namespace Game {

/**
 * @class Strings
 * @brief Tiny runtime UI string table loaded from Resources/data/strings.json
 *        (UTF-8). Keeping the Chinese labels in a DATA file (not source literals)
 *        avoids MSVC's C4819 code-page warning and needs no /utf-8 build change.
 *        Missing keys return the key itself, so untranslated text is visible.
 */
class Strings {
public:
    /// Idempotent: loads strings.json once (resourceRoot = RESOURCE_DIR).
    static void Load(const std::string &resourceRoot);
    /// @return the localized string for @p key, or @p key itself if absent.
    static std::string Get(const std::string &key);
};

} // namespace Game

#endif /* GAME_STRINGS_HPP */
