#include "entities/Chest.hpp"
#include "game/ChestAnim.hpp"
#include "game/WorldScale.hpp"
#include "game/RenderLayout.hpp"

#include <memory>
#include <utility>

namespace Game {

namespace {
constexpr float kChestRadius = 18.0F;
constexpr float kChestZIndex = RenderLayout::kZChest; // chest00/01 box05/06: sorting layer 4
constexpr float kScale = Scale::kSpriteScale; // 16 px per unit -> 32 px cell (shared rule)
/// PORT CHOICE (T-59): the chest's layer-4 parts keep the room-wide chest z (kZChest, between the gate
/// light and the gate); the prefab order step (box05 order 0, box06 order 2) is applied as 0.01 per
/// order so it stays inside the band.
constexpr float kChestOrderStepZ = 0.01F;

// Pre-P2.20 single-sprite fallback (FAITHFUL @ res/item/chest*.prefab SpriteRenderers, objects_common atlas).
const char *ClosedSprite(int tier, Chest::Kind kind) {
    if (kind == Chest::Kind::RoomReward) {
        return "objects_common_9";
    }
    if (kind == Chest::Kind::Boss || tier >= 2) {
        return "objects_common_13";
    }
    return tier == 1 ? "objects_common_3" : "objects_common_6";
}
const char *OpenSprite(int tier, Chest::Kind kind) {
    if (kind == Chest::Kind::RoomReward) {
        return "objects_common_8";
    }
    if (kind == Chest::Kind::Boss || tier >= 2) {
        return "objects_common_10";
    }
    return "objects_common_0";
}
} // namespace

const char *Chest::PrefabFor(int tier, Kind kind) {
    if (kind == Kind::RoomReward) {
        return "chest_room_reward";
    }
    if (kind == Kind::Boss || tier >= 2) {
        return "chest02";
    }
    return tier == 1 ? "chest01" : "chest00";
}

Chest::Layer Chest::MakeLayer(const std::string &resourceRoot, const RigNodeDef &node, float z) {
    Layer l;
    l.node = node;
    // Point-filtered like every other room sprite (the objects_common atlas is filterMode 0).
    l.image = std::make_shared<Util::Image>(resourceRoot + "/sprites/" + node.sprite, /*useAA=*/false);
    l.image->SetTint({node.color[0], node.color[1], node.color[2], node.color[3]});
    l.obj = std::make_shared<Util::GameObject>();
    l.obj->SetDrawable(l.image);
    l.obj->SetZIndex(z);
    return l;
}

void Chest::Place(Layer &l, glm::vec2 localUnits) {
    const ChestAnim::NodePlacement p = ChestAnim::Place(l.node, m_Feet, localUnits, l.image->GetSize());
    l.obj->m_Transform.translation = p.translation;
    l.obj->m_Transform.scale = p.scale;
    l.obj->SetPivot(p.pivotPx);
}

Chest::Chest(const std::string &resourceRoot, glm::vec2 pos, int tier, Kind kind, const RoomPropsDef *props)
    : m_Tier(tier), m_Kind(kind), m_Props(props) {
    m_Transform.translation = pos;
    if (props != nullptr) {
        const auto it = props->chests.find(PrefabFor(tier, kind));
        if (it != props->chests.end()) {
            m_Chest = &it->second;
        }
    }
    if (m_Chest == nullptr) {
        SetDrawable(std::make_shared<Util::Image>(resourceRoot + "/sprites/" +
                                                  ClosedSprite(tier, kind) + ".png"));
        m_OpenImage = std::make_shared<Util::Image>(resourceRoot + "/sprites/" +
                                                    OpenSprite(tier, kind) + ".png");
        SetZIndex(kChestZIndex);
        m_Transform.scale = {kScale, kScale};
        return;
    }
    // PORT CHOICE (T-76): the feet sit on the cell's bottom-centre like every room prefab (CellAnchor); the original
    // hangs RGChest at the b_point world position, the port keeps Position() = the cell centre for the open range.
    m_Rig = true;
    m_Feet = RenderLayout::CellAnchor(pos);
    const ChestPropsDef &c = *m_Chest;
    const float lidZ = RenderLayout::SortZ(c.box03.sortLayer, c.box03.sortOrder, m_Feet.y);
    m_Shadow = MakeLayer(resourceRoot, c.shadow, RenderLayout::SortZ(c.shadow.sortLayer, c.shadow.sortOrder, m_Feet.y));
    m_B05 = MakeLayer(resourceRoot, c.box05, kChestZIndex + static_cast<float>(c.box05.sortOrder) * kChestOrderStepZ);
    m_B06 = MakeLayer(resourceRoot, c.box06, kChestZIndex + static_cast<float>(c.box06.sortOrder) * kChestOrderStepZ);
    m_B03 = MakeLayer(resourceRoot, c.box03, lidZ);
    m_B04 = MakeLayer(resourceRoot, c.box04, lidZ);
    const ChestAnim::Pose closed = ChestAnim::ClosedPose(c);
    m_Box03 = closed.box03;
    m_Box04 = closed.box04;
    m_Box05 = closed.box05;
    for (Layer *l : {&m_Shadow, &m_B05, &m_B06, &m_B03, &m_B04}) {
        AddChild(l->obj);
    }
    Sync();
}

void Chest::Sync() {
    if (!m_Rig) {
        return;
    }
    Place(m_Shadow, {m_Chest->shadow.offsetX, m_Chest->shadow.offsetY});
    Place(m_B05, m_Box05);
    Place(m_B06, {m_Chest->box06.offsetX, m_Chest->box06.offsetY});
    Place(m_B03, m_Box03);
    Place(m_B04, m_Box04);
}

bool Chest::Open() {
    if (m_Opened) {
        return false;
    }
    m_Opened = true;
    if (!m_Rig) {
        SetDrawable(m_OpenImage);
        return true;
    }
    m_OpenSec = 0.0F;
    const ChestAnim::Pose p = ChestAnim::OpenPose(*m_Chest, m_Props->chestOpen, 0.0F);
    m_Box03 = p.box03;
    m_Box04 = p.box04;
    m_Box05 = p.box05;
    Sync();
    return true;
}

void Chest::Update(float dtMs) {
    if (!m_Rig || !m_Opened || m_OpenSec >= m_Props->chestOpen.stop) {
        return;
    }
    m_OpenSec = std::min(m_OpenSec + dtMs * 0.001F, m_Props->chestOpen.stop);
    const ChestAnim::Pose p = ChestAnim::OpenPose(*m_Chest, m_Props->chestOpen, m_OpenSec);
    m_Box03 = p.box03;
    m_Box04 = p.box04;
    m_Box05 = p.box05;
    Sync();
}

Util::Collider Chest::GetCollider() const {
    return Util::Collider::MakeCircle(m_Transform.translation, kChestRadius);
}

} // namespace Game
