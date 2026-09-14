#ifndef GAME_AUDIO_PLAYER_HPP
#define GAME_AUDIO_PLAYER_HPP

#include <string>

namespace Game {

/**
 * @class AudioPlayer
 * @brief Thin, data-driven wrapper over Util::SFX / Util::BGM.
 *
 * Callers hand it a clip *filename* (from Game::GameData's audio.json
 * accessors -- FindEffectClip/WeaponShootClip/EnemyGunShootClip/BgmFor); this
 * class resolves it to `<resourceRoot>/audio/<clip>` and plays it.
 *
 * @par Mute
 * `SK_MUTE=1` (any non-empty, non-"0" value) is a hard NO-OP switch, read once
 * at construction. Independently, the FIRST play call probes
 * `Mix_QuerySpec()`; if SDL_mixer's device never opened (headless box with no
 * audio device, `Mix_OpenAudio` failed in Core::Context), playback
 * auto-mutes too -- no per-call `Mix_GetError()` spam.
 *
 * @par Logging (proves the wiring in a headless run -- case H10)
 * Every non-empty clip logs exactly one INFO line -- `Audio: sfx <clip>` or
 * `Audio: bgm <clip>` -- UNCONDITIONALLY, even when muted or the mixer device
 * never opened. The log is proof the data-driven wiring fired; only the
 * actual `Mix_*` call is skipped when muted.
 */
class AudioPlayer {
public:
    explicit AudioPlayer(std::string resourceRoot);

    /// Play a one-shot sound effect. No-op if @p clip is empty or muted.
    void PlaySfx(const std::string &clip);

    /// Loop background music. No-op (not even a re-trigger) if @p clip equals
    /// whatever is already the current track, so callers can call this every
    /// frame/room-transition without restarting the loop.
    void PlayBgm(const std::string &clip, int loops = -1);

    /// Stop whatever BGM is playing (if any).
    void StopBgm();

    bool IsMuted() const { return m_Muted; }
    const std::string &CurrentBgm() const { return m_CurrentBgm; }

private:
    bool DeviceReady();

    std::string m_Root;
    bool m_Muted;
    bool m_DeviceChecked = false;
    bool m_DeviceOk = false;
    std::string m_CurrentBgm;
};

} // namespace Game

#endif // GAME_AUDIO_PLAYER_HPP
