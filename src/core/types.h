#pragma once

#include <cstdint>
#include <string>
#include <compare>
#include <vector>

namespace core {

// §1 — Logical timestamp: (lamport, deviceId) with total order.
struct Ver {
    int64_t lamport = 0;
    std::string deviceId;

    // Total order: lamport first, then lexicographic deviceId.
    auto operator<=>(const Ver& o) const noexcept {
        if (lamport != o.lamport)
            return lamport <=> o.lamport;
        return deviceId <=> o.deviceId;
    }
    bool operator==(const Ver& o) const noexcept = default;
};

// §2.1 — Shopping-list item. Each user-visible field carries its own version.
struct Item {
    // Immutable identity (set at creation, never versioned).
    std::string listId;
    std::string itemId;
    int64_t     created = 0; // ms epoch
    std::string by;          // deviceId of creator

    // Versioned fields (LWW).
    std::string name;
    Ver         nameVer;

    std::string qty;
    Ver         qtyVer;

    // Précision libre : « 6 couches épaisses », « la marque bleue », « sans sucre ».
    std::string note;
    Ver         noteVer;

    // Rayon du magasin (« Crèmerie »), vide si non classé. Le libellé lui-même sert de
    // clé : un pair qui ne connaît pas un rayon l'affiche quand même, sans rien perdre.
    std::string aisle;
    Ver         aisleVer;

    // Position manuelle dans la liste. Valeurs espacées (initialisées sur `created`,
    // en millisecondes) : réordonner = se glisser au milieu de l'intervalle voisin,
    // sans avoir à renuméroter les autres — donc sans les faire entrer en conflit.
    int64_t order = 0;
    Ver     orderVer;

    bool done = false;
    Ver  doneVer;
    // Date de cochage (ms epoch, 0 si à acheter). Satellite de `done` : pas de version
    // propre, il suit doneVer — celui qui gagne le merge sur `done` apporte sa date.
    int64_t doneAt = 0;

    bool del = false;
    Ver  delVer;

    // Local-only metadata (§2.4 / §6): ms epoch of last local write. Never synchronised.
    int64_t touched = 0;
};

// §2.2 — List metadata.
struct ListMeta {
    std::string listId;
    std::vector<uint8_t> key; // 32 bytes

    std::string title;
    Ver         titleVer;

    int64_t lamport  = 0;  // current Lamport clock for this list
    int64_t lastSync = 0;  // ms epoch of last relay sync
    int64_t created  = 0;  // ms epoch of list creation
};

} // namespace core
