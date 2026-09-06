#include "entities/VendingMachine.hpp"

#include <memory>

#include "Util/Animation.hpp"
#include "Util/Color.hpp"

#include <vector>

namespace Game {

namespace {
constexpr float kScale = 2.0F; // 16 px per unit -> 32 px cell
constexpr float kZBody = 1.3F;
constexpr float kZDisplay = 1.32F;
constexpr float kZIcon = 1.33F;
constexpr float kZText = 1.34F;
} // namespace

VendingMachine::VendingMachine(const std::string &resourceRoot, glm::vec2 pos, Kind kind,
                               int price, const std::string &bodySprite,
                               const std::string &displaySprite, const std::string &stockId,
                               const std::string &stockSprite)
    : m_Kind(kind), m_Price(price), m_Stock(stockId) {
    auto body = std::make_shared<Util::Image>(resourceRoot + "/sprites/" + bodySprite + ".png");
    const glm::vec2 sz = body->GetSize();
    if (kind == Kind::Water) {
        // FAITHFUL @ water_dispenser.prefab: its Animator is rganim/npc/naughty_boy_1
        // (state postman_ide: naughtyboy_1_4..7) -- in 1.7.10 the "dispenser" is the
        // naughty-boy NPC vendor, not a machine sprite.
        std::vector<std::string> frames;
        for (int i = 4; i <= 7; ++i) {
            frames.push_back(resourceRoot + "/sprites/naughtyboy_1_" + std::to_string(i) + ".png");
        }
        SetDrawable(std::make_shared<Util::Animation>(frames, true, 150, true));
    } else {
        SetDrawable(body);
    }
    SetZIndex(kZBody);
    m_Transform.scale = {kScale, kScale};
    // Bottom-anchored like every room object: the prefab pivot is (0.5, 0).
    m_Transform.translation = pos + glm::vec2(0.0F, sz.y * kScale * 0.5F - 16.0F);

    const float top = pos.y + sz.y * kScale - 16.0F;
    if (!displaySprite.empty()) {
        m_Display = std::make_shared<Util::GameObject>();
        auto disp = std::make_shared<Util::Image>(resourceRoot + "/sprites/" + displaySprite + ".png");
        m_Display->SetDrawable(disp);
        m_Display->m_Transform.scale = {kScale, kScale};
        m_Display->m_Transform.translation = {pos.x, top + disp->GetSize().y * kScale * 0.5F + 4.0F};
        m_Display->SetZIndex(kZDisplay);
    }
    if (!stockSprite.empty()) {
        m_Icon = std::make_shared<Util::GameObject>();
        m_Icon->SetDrawable(std::make_shared<Util::Image>(resourceRoot + "/sprites/" + stockSprite + ".png"));
        m_Icon->m_Transform.scale = {kScale, kScale};
        m_Icon->m_Transform.translation = {pos.x, top + 20.0F};
        m_Icon->SetZIndex(kZIcon);
    }
    m_PriceTag = std::make_shared<Util::GameObject>();
    m_PriceText = std::make_shared<Util::Text>(resourceRoot + "/fonts/pixel_bold.ttf", 14,
                                               price > 0 ? std::to_string(price) + " G" : "FREE",
                                               Util::Color{255, 230, 120, 255});
    m_PriceTag->SetDrawable(m_PriceText);
    m_PriceTag->m_Transform.translation = {pos.x, top + 48.0F};
    m_PriceTag->SetZIndex(kZText);
}

void VendingMachine::MarkSold() {
    m_Sold = true;
    if (m_PriceText) {
        m_PriceText->SetText("SOLD");
    }
    if (m_Icon) {
        m_Icon->m_Transform.scale = {0.0F, 0.0F}; // stock taken
    }
}

} // namespace Game
