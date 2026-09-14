#include "game/AudioPlayer.hpp"

#include <cstdlib>
#include <utility>

#include <SDL_mixer.h>

#include "Util/BGM.hpp"
#include "Util/Logger.hpp"
#include "Util/SFX.hpp"

namespace Game {
namespace {
bool EnvMuteRequested() {
    const char *v = std::getenv("SK_MUTE");
    return v != nullptr && v[0] != '\0' && v[0] != '0';
}
} // namespace

AudioPlayer::AudioPlayer(std::string resourceRoot)
    : m_Root(std::move(resourceRoot)), m_Muted(EnvMuteRequested()) {}

bool AudioPlayer::DeviceReady() {
    if (!m_DeviceChecked) {
        m_DeviceChecked = true;
        int freq = 0;
        Uint16 fmt = 0;
        int channels = 0;
        m_DeviceOk = Mix_QuerySpec(&freq, &fmt, &channels) != 0;
        if (!m_DeviceOk) {
            LOG_DEBUG("AudioPlayer: SDL_mixer device not open -- auto-muting");
            m_Muted = true;
        }
    }
    return m_DeviceOk;
}

void AudioPlayer::PlaySfx(const std::string &clip) {
    if (clip.empty()) {
        return;
    }
    // Log unconditionally (even muted/device-less) -- this line IS the proof a
    // headless run wires real data-driven clips (case H10), not just that the
    // hardware made noise.
    LOG_INFO("Audio: sfx {}", clip);
    if (m_Muted || !DeviceReady()) {
        return;
    }
    // Util::SFX itself caches the decoded Mix_Chunk by path (Util::AssetStore),
    // so constructing this wrapper per call is cheap.
    Util::SFX sfx(m_Root + "/audio/" + clip);
    sfx.Play();
}

void AudioPlayer::PlayBgm(const std::string &clip, int loops) {
    if (clip.empty() || clip == m_CurrentBgm) {
        return;
    }
    m_CurrentBgm = clip;
    LOG_INFO("Audio: bgm {}", clip); // see PlaySfx: logged even when muted/device-less.
    if (m_Muted || !DeviceReady()) {
        return;
    }
    Util::BGM bgm(m_Root + "/audio/" + clip);
    bgm.Play(loops);
}

void AudioPlayer::StopBgm() {
    m_CurrentBgm.clear();
    if (m_Muted || !DeviceReady()) {
        return;
    }
    Mix_HaltMusic();
}

} // namespace Game
