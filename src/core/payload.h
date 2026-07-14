#pragma once

#include "types.h"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace core {

// §4 — Parsed representation of a delta or snap payload.
struct Payload {
    enum class Type { delta, snap };

    Type        type = Type::delta;
    std::string listId;

    // deviceId de l'émetteur. Sans lui, le receveur devine l'auteur en prenant la
    // première entrée de members — arbitraire dans un snap, qui les porte toutes.
    // Champ optionnel : absent des payloads émis par les versions antérieures.
    std::string by;

    std::vector<Item> items;

    // snap-only fields (also allowed in delta when changed)
    std::optional<std::string> title;
    std::optional<Ver>         titleVer;

    // deviceId -> (displayName, ver)
    std::map<std::string, std::pair<std::string, Ver>> members;
};

// Deserialize a JSON string into a Payload.
// Returns nullopt if "v" != 1, type is unrecognised, or JSON is malformed at
// the top level. Individual malformed items are silently skipped (§4 rule).
// Unknown fields are ignored (forward-compat).
std::optional<Payload> parsePayload(const std::string& json);

// Serialize a Payload to compact JSON.
std::string serializePayload(const Payload& p);

} // namespace core
