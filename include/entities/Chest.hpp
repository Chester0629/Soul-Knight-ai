#ifndef GAME_CHEST_HPP
#define GAME_CHEST_HPP

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"
#include "Util/GameObject.hpp"
#include "Util/Image.hpp"

#include "data/GameData.hpp"

namespace Game {

/**
 * @class Chest
 * @brief An openable loot chest -- the port of the RGChest family
 *        (res/item/chest00..02, chest_room_reward, chest_boss<NN>): a drop tier
 *        (RGChest.level -> LootTable::Roll), the guaranteed signature weapons of a boss chest
 *        (RGChestBoss.object_list) and, since P2.20, the original's FOUR layers + shadow.
 *
 * P2.20 FAITHFUL @ res/item/chest*.prefab (room_props.json `chests`) + rganim/chest_open.anim: the
 * chest is the interior box05, the front panel box06, the two lid halves box03 / box04 and the
 * shadow1 ground shadow (alpha 0.392, scale (1.2,1)); Open() plays chest_open (the lids slide apart
 * and the interior rises over ~0.5 s, see game/ChestAnim.hpp). The layers are children of this
 * object, so the scene still adds the Chest itself to its renderer. Without room_props data
 * (@c props == nullptr) the pre-P2.20 single-sprite chest (closed panel / open interior swap) is kept.
 *
 * Prefab per kind: chest_room_reward (RoomReward), chest02 (Boss, tier >= 2 -- chest_boss<NN> have the
 * same art), chest01 (tier 1), chest00 (tier 0).
 */
class Chest : public Util::GameObject {
public:
    enum class Kind { Small, RoomReward, Boss };

    /// @p props (optional, non-owning, must outlive the chest): GameData::RoomProps().
    Chest(const std::string &resourceRoot, glm::vec2 pos, int tier, Kind kind = Kind::Small,
          const RoomPropsDef *props = nullptr);

    /// The res/item prefab whose layers a (tier, kind) chest draws (room_props.json key).
    static const char *PrefabFor(int tier, Kind kind);

    int Tier() const { return m_Tier; }
    Kind GetKind() const { return m_Kind; }
    bool Opened() const { return m_Opened; }
    /// Open: start chest_open (or swap to the open sprite without props). Returns false if already open.
    bool Open();
    /// Advance chest_open by @p dtMs (no-op before Open() and once the clip has finished).
    void Update(float dtMs) override;
    /// Seconds since Open() (0 before).
    float OpenSeconds() const { return m_OpenSec; }
    /// True when this chest draws the P2.20 four-layer rig.
    bool HasRig() const { return m_Rig; }
    /// The current local positions (units) of the animated layers (rig chests only).
    glm::vec2 Box03() const { return m_Box03; }
    glm::vec2 Box04() const { return m_Box04; }
    glm::vec2 Box05() const { return m_Box05; }
    /// Feet (the prefab root) in world px.
    glm::vec2 Feet() const { return m_Feet; }
    /// RGChestBoss.object_list: weapons this chest always contains.
    void SetGuaranteed(std::vector<std::string> weaponIds) { m_Guaranteed = std::move(weaponIds); }
    const std::vector<std::string> &Guaranteed() const { return m_Guaranteed; }

    glm::vec2 Position() const { return m_Transform.translation; }
    Util::Collider GetCollider() const;

private:
    struct Layer {
        std::shared_ptr<Util::GameObject> obj;
        std::shared_ptr<Util::Image> image;
        RigNodeDef node;
    };
    Layer MakeLayer(const std::string &resourceRoot, const RigNodeDef &node, float z);
    void Place(Layer &l, glm::vec2 localUnits);
    void Sync();

    int m_Tier = 0;
    Kind m_Kind = Kind::Small;
    bool m_Opened = false;
    std::shared_ptr<Util::Image> m_OpenImage; ///< single-sprite fallback only
    std::vector<std::string> m_Guaranteed;

    // --- P2.20 four-layer rig ---
    bool m_Rig = false;
    const RoomPropsDef *m_Props = nullptr;
    const ChestPropsDef *m_Chest = nullptr;
    Layer m_Shadow, m_B03, m_B04, m_B05, m_B06;
    glm::vec2 m_Feet{0.0F, 0.0F};
    glm::vec2 m_Box03{0.0F, 0.0F};
    glm::vec2 m_Box04{0.0F, 0.0F};
    glm::vec2 m_Box05{0.0F, 0.0F};
    float m_OpenSec = 0.0F;
};

} // namespace Game

#endif /* GAME_CHEST_HPP */
