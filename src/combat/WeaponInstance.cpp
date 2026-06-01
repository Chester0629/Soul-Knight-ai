#include "combat/WeaponInstance.hpp"

#include <algorithm>

namespace Game {

WeaponInstance::WeaponInstance(const WeaponDef &def)
    : m_Def(&def),
      m_FireIntervalMs(kBaseIntervalMs / std::max(def.weaponSpeed, 0.01F)) {}

void WeaponInstance::Update(float dtMs) {
    if (dtMs > 0.0F && m_CooldownMs > 0.0F) {
        m_CooldownMs -= dtMs;
        if (m_CooldownMs < 0.0F) {
            m_CooldownMs = 0.0F;
        }
    }
}

bool WeaponInstance::Ready() const { return m_CooldownMs <= 0.0F; }

bool WeaponInstance::TryFire() {
    if (!Ready()) {
        return false;
    }
    m_CooldownMs = m_FireIntervalMs;
    return true;
}

int WeaponInstance::EnergyCost() const { return m_Def->consume; }

} // namespace Game
