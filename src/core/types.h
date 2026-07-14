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

    bool done = false;
    Ver  doneVer;

    bool del = false;
    Ver  delVer;
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
