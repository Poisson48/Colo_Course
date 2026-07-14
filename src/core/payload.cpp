#include "payload.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

using json = nlohmann::json;

namespace core {

// Helper: parse a versioned field array [value, [lamport, deviceId]].
// Returns false if the structure is malformed.
template <typename T>
static bool parseVersionedField(const json& arr, T& value, Ver& ver) {
    if (!arr.is_array() || arr.size() != 2) return false;
    const json& verArr = arr[1];
    if (!verArr.is_array() || verArr.size() != 2) return false;
    if (!verArr[0].is_number_integer()) return false;
    if (!verArr[1].is_string()) return false;

    try {
        if constexpr (std::is_same_v<T, std::string>) {
            if (!arr[0].is_string()) return false;
            value = arr[0].get<std::string>();
        } else if constexpr (std::is_same_v<T, bool>) {
            if (!arr[0].is_boolean()) return false;
            value = arr[0].get<bool>();
        } else if constexpr (std::is_same_v<T, int64_t>) {
            if (!arr[0].is_number_integer()) return false;
            value = arr[0].get<int64_t>();
        } else {
            return false;
        }
        ver.lamport  = verArr[0].get<int64_t>();
        ver.deviceId = verArr[1].get<std::string>();
    } catch (...) {
        return false;
    }
    return true;
}

std::optional<Payload> parsePayload(const std::string& jsonStr) {
    json j;
    try {
        j = json::parse(jsonStr);
    } catch (...) {
        return std::nullopt;
    }

    if (!j.is_object()) return std::nullopt;

    // "v" must be 1
    if (!j.contains("v") || j["v"] != 1) return std::nullopt;

    // "t" must be "delta" or "snap"
    if (!j.contains("t") || !j["t"].is_string()) return std::nullopt;
    const std::string typeStr = j["t"].get<std::string>();
    Payload::Type type;
    if (typeStr == "delta") {
        type = Payload::Type::delta;
    } else if (typeStr == "snap") {
        type = Payload::Type::snap;
    } else {
        return std::nullopt;
    }

    // "list"
    if (!j.contains("list") || !j["list"].is_string()) return std::nullopt;
    std::string listId = j["list"].get<std::string>();

    Payload p;
    p.type   = type;
    p.listId = std::move(listId);

    // Parse items array (individual malformed items are silently skipped)
    if (j.contains("items") && j["items"].is_array()) {
        for (const auto& jitem : j["items"]) {
            if (!jitem.is_object()) continue;

            Item item;

            // Required fields
            if (!jitem.contains("id") || !jitem["id"].is_string()) continue;
            item.itemId = jitem["id"].get<std::string>();
            item.listId = p.listId;

            if (jitem.contains("created") && jitem["created"].is_number_integer()) {
                item.created = jitem["created"].get<int64_t>();
            }
            if (jitem.contains("by") && jitem["by"].is_string()) {
                item.by = jitem["by"].get<std::string>();
            }
            if (jitem.contains("doneAt") && jitem["doneAt"].is_number_integer()) {
                item.doneAt = jitem["doneAt"].get<int64_t>();
            }

            if (!jitem.contains("f") || !jitem["f"].is_object()) continue;
            const json& f = jitem["f"];

            // Parse each versioned field; skip the entire item only if we can't
            // get *any* useful field (but spec says malformed items are ignored individually).
            bool anyField = false;

            if (f.contains("name")) {
                if (parseVersionedField(f["name"], item.name, item.nameVer)) anyField = true;
            }
            if (f.contains("qty")) {
                if (parseVersionedField(f["qty"], item.qty, item.qtyVer)) anyField = true;
            }
            // Absent des payloads émis par les versions antérieures : la note reste vide
            // en version {0,""}, que toute note réelle bat au merge (jamais d'écrasement).
            if (f.contains("note")) {
                if (parseVersionedField(f["note"], item.note, item.noteVer)) anyField = true;
            }
            if (f.contains("aisle")) {
                if (parseVersionedField(f["aisle"], item.aisle, item.aisleVer)) anyField = true;
            }
            if (f.contains("order")) {
                if (parseVersionedField(f["order"], item.order, item.orderVer)) anyField = true;
            }
            if (f.contains("done")) {
                if (parseVersionedField(f["done"], item.done, item.doneVer)) anyField = true;
            }
            if (f.contains("del")) {
                if (parseVersionedField(f["del"], item.del, item.delVer)) anyField = true;
            }

            if (!anyField && !f.empty()) {
                // All known fields malformed — skip item
                continue;
            }

            p.items.push_back(std::move(item));
        }
    }

    // Parse optional author deviceId.
    if (j.contains("by") && j["by"].is_string())
        p.by = j["by"].get<std::string>();

    // Parse optional title (snap or delta-with-title)
    if (j.contains("title")) {
        std::string title;
        Ver         ver;
        if (parseVersionedField(j["title"], title, ver)) {
            p.title    = std::move(title);
            p.titleVer = ver;
        }
    }

    // Parse optional members
    if (j.contains("members") && j["members"].is_object()) {
        for (const auto& [devId, mval] : j["members"].items()) {
            std::string name;
            Ver         ver;
            if (parseVersionedField(mval, name, ver)) {
                p.members[devId] = {std::move(name), ver};
            }
        }
    }

    return p;
}

static json verToJson(const Ver& ver) {
    return json::array({ver.lamport, ver.deviceId});
}

std::string serializePayload(const Payload& p) {
    json j;
    j["v"]    = 1;
    j["t"]    = (p.type == Payload::Type::delta) ? "delta" : "snap";
    j["list"] = p.listId;
    if (!p.by.empty())
        j["by"] = p.by;

    json items = json::array();
    for (const auto& item : p.items) {
        json ji;
        ji["id"]      = item.itemId;
        ji["created"] = item.created;
        ji["by"]      = item.by;
        ji["doneAt"]  = item.doneAt;

        json f;
        f["name"] = json::array({item.name, verToJson(item.nameVer)});
        f["qty"]  = json::array({item.qty,  verToJson(item.qtyVer)});
        f["note"]  = json::array({item.note,  verToJson(item.noteVer)});
        f["aisle"] = json::array({item.aisle, verToJson(item.aisleVer)});
        f["order"] = json::array({item.order, verToJson(item.orderVer)});
        f["done"] = json::array({item.done, verToJson(item.doneVer)});
        f["del"]  = json::array({item.del,  verToJson(item.delVer)});
        ji["f"]   = f;

        items.push_back(ji);
    }
    j["items"] = items;

    if (p.title.has_value() && p.titleVer.has_value()) {
        j["title"] = json::array({*p.title, verToJson(*p.titleVer)});
    }

    if (!p.members.empty()) {
        json members;
        for (const auto& [devId, nv] : p.members) {
            members[devId] = json::array({nv.first, verToJson(nv.second)});
        }
        j["members"] = members;
    }

    return j.dump();
}

} // namespace core
