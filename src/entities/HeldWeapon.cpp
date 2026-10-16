#include "entities/HeldWeapon.hpp"

#include "game/ActorPose.hpp"
#include "game/RenderLayout.hpp"
#include "game/WorldScale.hpp"

namespace Game {

void HeldWeapon::SetWeapon(const WeaponDef *def, const std::string &resourceRoot, int handLayer) {
    m_Def = HeldWeaponDef{};
    m_Visible = false;
    if (def == nullptr || !def->held.present) {
        if (m_Object != nullptr) {
            m_Object->SetVisible(false);
        }
        return;
    }
    m_Def = def->held;
    // Point-filtered like every other sprite of the original (filterMode 0): the 13x9 gun must not blur.
    m_Image = std::make_shared<Util::Image>(resourceRoot + "/sprites/" + m_Def.sprite.sprite, /*useAA=*/false);
    m_Image->SetTint({m_Def.sprite.color[0], m_Def.sprite.color[1], m_Def.sprite.color[2], m_Def.sprite.color[3]});
    if (m_Object == nullptr) {
        m_Object = std::make_shared<Util::GameObject>();
    }
    m_Object->SetDrawable(m_Image);
    // INFERRED (T-69 (4)): the hand mount's RGHand.target_layer replaces the weapon prefab's own layer (5); the order
    // stays. RGHand.SetUpWeapon is truncated, so this is read from the ground-truth draw order, not seen in code.
    m_Object->SetZIndex(RenderLayout::SortZ(handLayer, m_Def.sprite.sortOrder));
    m_Object->SetVisible(true);
    m_Visible = true;
}

void HeldWeapon::Sync(glm::vec2 position, glm::vec2 aim, int facing, glm::vec2 handRootPx, glm::vec2 feetOffsetPx) {
    if (!Visible()) {
        return;
    }
    const glm::vec2 hand = ActorPose::HandWorld(position, handRootPx, feetOffsetPx, facing);
    const glm::vec2 localOffset{Scale::UnitsToPx(m_Def.sprite.offsetX), Scale::UnitsToPx(m_Def.sprite.offsetY)};
    const ActorPose::SpritePose pose = ActorPose::WeaponSpritePose(aim, facing);
    const float s = Scale::kSpriteScale;
    m_Object->m_Transform.translation = ActorPose::WeaponPointWorld(hand, localOffset, aim, facing);
    m_Object->m_Transform.rotation = pose.rotationRad;
    m_Object->m_Transform.scale = {pose.scaleSignX * s * m_Def.sprite.scaleX, s * m_Def.sprite.scaleY};
    const glm::vec2 size = m_Image->GetSize();
    m_Object->SetPivot({(m_Def.sprite.pivotX - 0.5F) * size.x, (m_Def.sprite.pivotY - 0.5F) * size.y});
}

glm::vec2 HeldWeapon::Muzzle(glm::vec2 position, glm::vec2 aim, int facing, glm::vec2 handRootPx,
                             glm::vec2 feetOffsetPx) const {
    if (!m_Def.present || !m_Def.hasGunPoint) {
        return position;
    }
    const glm::vec2 hand = ActorPose::HandWorld(position, handRootPx, feetOffsetPx, facing);
    return ActorPose::WeaponPointWorld(
        hand, {Scale::UnitsToPx(m_Def.gunPointX), Scale::UnitsToPx(m_Def.gunPointY)}, aim, facing);
}

} // namespace Game
