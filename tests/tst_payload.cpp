// tst_payload.cpp — JSON payload round-trip and resilience tests (SPEC §4)
// Pure C++ / STL, no Qt dependency.

#include "core/payload.h"
#include "core/types.h"

#include <cstdio>
#include <optional>
#include <string>

using namespace core;

// ---------------------------------------------------------------------------
// Minimal test framework (mirrors tst_crdt.cpp)
// ---------------------------------------------------------------------------

static int g_total  = 0;
static int g_passed = 0;
static int g_failed = 0;

#define EXPECT_TRUE(expr)                                               \
    do {                                                                \
        ++g_total;                                                      \
        if (!(expr)) {                                                  \
            ++g_failed;                                                 \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n",                  \
                         __FILE__, __LINE__, #expr);                    \
        } else {                                                        \
            ++g_passed;                                                 \
        }                                                               \
    } while (0)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))
#define EXPECT_EQ(a, b)   EXPECT_TRUE((a) == (b))

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Ver makeVer(int64_t l, const std::string& dev) {
    Ver v; v.lamport = l; v.deviceId = dev; return v;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Auteur, description et date de cochage font l'aller-retour.
static void test_AuthorNoteDoneAtRoundTrip() {
    Payload p;
    p.type   = Payload::Type::delta;
    p.listId = "list-abc";
    p.by     = "devA";

    Item item;
    item.listId  = "list-abc";
    item.itemId  = "item-001";
    item.created = 1721000000000LL;
    item.by      = "devA";
    item.name    = "Papier toilette";
    item.nameVer = makeVer(3, "devA");
    item.qty     = "2 paquets";
    item.qtyVer  = makeVer(3, "devA");
    item.note    = "6 couches épaisses";
    item.noteVer = makeVer(4, "devA");
    item.done    = true;
    item.doneVer = makeVer(5, "devA");
    item.doneAt  = 1721000900000LL;
    p.items.push_back(item);

    p.members["devA"] = {"Marie", makeVer(3, "devA")};

    auto parsed = parsePayload(serializePayload(p));
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->by, "devA");
    EXPECT_EQ(parsed->items.size(), size_t(1));

    const Item& pi = parsed->items[0];
    EXPECT_EQ(pi.note,            "6 couches épaisses");
    EXPECT_EQ(pi.noteVer.lamport, int64_t(4));
    EXPECT_EQ(pi.doneAt,          int64_t(1721000900000LL));

    // L'auteur se retrouve par son deviceId : c'est ce qui nomme la notification.
    auto author = parsed->members.find(parsed->by);
    EXPECT_TRUE(author != parsed->members.end());
    EXPECT_EQ(author->second.first, "Marie");
}

// Un payload émis par la version précédente n'a ni "by", ni "note", ni "doneAt" :
// il doit rester lisible, et ses champs absents valoir « rien » (version {0,""}),
// pour qu'un merge ne les prenne jamais pour un effacement volontaire.
static void test_LegacyPayloadWithoutNewFields() {
    const std::string legacy = R"({
        "v": 1, "t": "delta", "list": "list-abc",
        "items": [{
            "id": "item-001", "created": 1721000000000, "by": "devA",
            "f": {
                "name": ["Lait", [12, "devA"]],
                "qty":  ["1L",   [12, "devA"]],
                "done": [false,  [12, "devA"]],
                "del":  [false,  [12, "devA"]]
            }
        }],
        "members": {"devA": ["Marie", [12, "devA"]]}
    })";

    auto parsed = parsePayload(legacy);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->by.empty());
    EXPECT_EQ(parsed->items.size(), size_t(1));

    const Item& pi = parsed->items[0];
    EXPECT_EQ(pi.name,            "Lait");
    EXPECT_EQ(pi.note,            "");
    EXPECT_EQ(pi.noteVer.lamport, int64_t(0));
    EXPECT_EQ(pi.noteVer.deviceId, "");
    EXPECT_EQ(pi.doneAt,          int64_t(0));
}

static void test_DeltaRoundTrip() {
    Payload p;
    p.type   = Payload::Type::delta;
    p.listId = "list-abc";

    Item item;
    item.listId  = "list-abc";
    item.itemId  = "item-001";
    item.created = 1721000000000LL;
    item.by      = "devA";
    item.name    = "Lait";
    item.nameVer = makeVer(12, "devA");
    item.qty     = "1L";
    item.qtyVer  = makeVer(12, "devA");
    item.done    = true;
    item.doneVer = makeVer(15, "devB");
    item.del     = false;
    item.delVer  = makeVer(12, "devA");
    p.items.push_back(item);

    std::string json   = serializePayload(p);
    auto        parsed = parsePayload(json);

    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->type,   Payload::Type::delta);
    EXPECT_EQ(parsed->listId, "list-abc");
    EXPECT_EQ(parsed->items.size(), size_t(1));

    const Item& pi = parsed->items[0];
    EXPECT_EQ(pi.itemId,         "item-001");
    EXPECT_EQ(pi.created,        int64_t(1721000000000LL));
    EXPECT_EQ(pi.by,             "devA");
    EXPECT_EQ(pi.name,           "Lait");
    EXPECT_EQ(pi.nameVer.lamport, int64_t(12));
    EXPECT_EQ(pi.nameVer.deviceId,"devA");
    EXPECT_EQ(pi.qty,            "1L");
    EXPECT_TRUE(pi.done);
    EXPECT_EQ(pi.doneVer.lamport, int64_t(15));
    EXPECT_FALSE(pi.del);
}

static void test_SnapRoundTrip() {
    Payload p;
    p.type      = Payload::Type::snap;
    p.listId    = "snap-list";
    p.title     = "Courses";
    p.titleVer  = makeVer(3, "devA");
    p.members["devA"] = {"Léo",  makeVer(1, "devA")};
    p.members["devB"] = {"Marie", makeVer(2, "devB")};

    // Add one item
    Item item;
    item.listId  = "snap-list";
    item.itemId  = "item-s1";
    item.created = 999;
    item.by      = "devA";
    item.name    = "Beurre";
    item.nameVer = makeVer(1, "devA");
    p.items.push_back(item);

    std::string json   = serializePayload(p);
    auto        parsed = parsePayload(json);

    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->type, Payload::Type::snap);
    EXPECT_TRUE(parsed->title.has_value());
    EXPECT_EQ(*parsed->title, "Courses");
    EXPECT_TRUE(parsed->titleVer.has_value());
    EXPECT_EQ(parsed->titleVer->lamport, int64_t(3));
    EXPECT_EQ(parsed->members.size(), size_t(2));
    EXPECT_EQ(parsed->members.at("devA").first, "Léo");
    EXPECT_EQ(parsed->members.at("devB").first, "Marie");
    EXPECT_EQ(parsed->items.size(), size_t(1));
    EXPECT_EQ(parsed->items[0].name, "Beurre");
}

static void test_MalformedJson() {
    EXPECT_FALSE(parsePayload("{not valid json").has_value());
    EXPECT_FALSE(parsePayload("null").has_value());
    EXPECT_FALSE(parsePayload("[]").has_value());
    EXPECT_FALSE(parsePayload("").has_value());
}

static void test_WrongVersion() {
    EXPECT_FALSE(parsePayload(R"({"v":2,"t":"delta","list":"l","items":[]})").has_value());
    EXPECT_FALSE(parsePayload(R"({"v":0,"t":"delta","list":"l","items":[]})").has_value());
    EXPECT_FALSE(parsePayload(R"({"t":"delta","list":"l","items":[]})").has_value());
}

static void test_UnknownTypeRejected() {
    EXPECT_FALSE(parsePayload(R"({"v":1,"t":"unknown","list":"l","items":[]})").has_value());
}

static void test_UnknownFieldsIgnored() {
    // Extra unknown fields at top level and inside item
    std::string json = R"({
        "v":1,"t":"delta","list":"mylist",
        "future_extension":"hello",
        "items":[
          {"id":"i1","created":1,"by":"devA",
           "unknown_field":42,
           "f":{"name":["X",[1,"devA"]]}}
        ]
    })";
    auto p = parsePayload(json);
    EXPECT_TRUE(p.has_value());
    EXPECT_EQ(p->items.size(), size_t(1));
    EXPECT_EQ(p->items[0].name, "X");
}

static void test_MalformedItemIgnored_OthersKept() {
    // Item 1 is valid, item 2 has malformed "f" (missing), item 3 is valid
    std::string json = R"({
        "v":1,"t":"delta","list":"l",
        "items":[
          {"id":"i1","created":1,"by":"devA","f":{"name":["Good",[1,"devA"]]}},
          {"id":"i2","created":2,"by":"devB"},
          {"id":"i3","created":3,"by":"devC","f":{"qty":["500g",[2,"devC"]]}}
        ]
    })";
    auto p = parsePayload(json);
    EXPECT_TRUE(p.has_value());
    // Item 2 has no "f" → skipped; items 1 and 3 kept
    EXPECT_EQ(p->items.size(), size_t(2));
    EXPECT_EQ(p->items[0].itemId, "i1");
    EXPECT_EQ(p->items[1].itemId, "i3");
}

static void test_EmptyItems() {
    auto p = parsePayload(R"({"v":1,"t":"delta","list":"L","items":[]})");
    EXPECT_TRUE(p.has_value());
    EXPECT_TRUE(p->items.empty());
}

static void test_ItemWithPartialFields() {
    // Only qty field set (name/done/del have zero versions by default)
    std::string json = R"({
        "v":1,"t":"delta","list":"l",
        "items":[
          {"id":"i1","created":0,"by":"devA","f":{"qty":["2kg",[5,"devA"]]}}
        ]
    })";
    auto p = parsePayload(json);
    EXPECT_TRUE(p.has_value());
    EXPECT_EQ(p->items.size(), size_t(1));
    EXPECT_EQ(p->items[0].qty, "2kg");
    EXPECT_EQ(p->items[0].name, ""); // not in payload → default
}

static void test_MalformedVersionArray() {
    // Ver array has wrong shape
    std::string json = R"({
        "v":1,"t":"delta","list":"l",
        "items":[
          {"id":"i1","created":0,"by":"devA","f":{"name":["X","not-an-array"]}},
          {"id":"i2","created":0,"by":"devA","f":{"name":["Y",[1,"devA"]]}}
        ]
    })";
    auto p = parsePayload(json);
    EXPECT_TRUE(p.has_value());
    // i1: name field malformed (ver not array), but no other valid field → item skipped
    // i2: valid
    EXPECT_EQ(p->items.size(), size_t(1));
    EXPECT_EQ(p->items[0].itemId, "i2");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== tst_payload ===\n");

    test_DeltaRoundTrip();
    test_AuthorNoteDoneAtRoundTrip();
    test_LegacyPayloadWithoutNewFields();
    test_SnapRoundTrip();
    test_MalformedJson();
    test_WrongVersion();
    test_UnknownTypeRejected();
    test_UnknownFieldsIgnored();
    test_MalformedItemIgnored_OthersKept();
    test_EmptyItems();
    test_ItemWithPartialFields();
    test_MalformedVersionArray();

    std::printf("\nResults: %d/%d passed, %d failed\n", g_passed, g_total, g_failed);
    return (g_failed == 0) ? 0 : 1;
}
