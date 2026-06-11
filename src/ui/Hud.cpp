#include "ui/Hud.hpp"

#include "pch.hpp" // provides imgui.h

namespace Game {

void Hud::Draw(const CombatStats &player) {
    ImGui::SetNextWindowPos(ImVec2(10.0F, 10.0F));
    ImGui::Begin("HUD", nullptr,
                 ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize);

    // Numeric readouts.
    ImGui::Text("HP: %d / %d", player.hp, player.maxHp);
    ImGui::Text("Armor: %d / %d", player.armor, player.maxArmor);
    ImGui::Text("Energy: %d / %d", player.energy, player.maxEnergy);

    // HP progress bar; guard against divide-by-zero when maxHp is unset.
    const float hpFraction =
        player.maxHp > 0
            ? static_cast<float>(player.hp) / static_cast<float>(player.maxHp)
            : 0.0F;
    ImGui::ProgressBar(hpFraction, ImVec2(-1.0F, 0.0F));

    ImGui::End();
}

} // namespace Game
