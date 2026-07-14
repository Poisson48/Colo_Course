qt_add_executable(tst_smoke
    tst_smoke.cpp
)

target_link_libraries(tst_smoke PRIVATE
    Qt6::Test
)

add_test(NAME smoke COMMAND tst_smoke)

# --- Database tests ---
qt_add_executable(tst_database
    tst_database.cpp
)

target_link_libraries(tst_database PRIVATE
    store
    Qt6::Test
    Qt6::Sql
)

add_test(NAME database COMMAND tst_database)

# --- CRDT unit tests (pure C++, no Qt) ---
add_executable(tst_crdt
    tst_crdt.cpp
)

target_include_directories(tst_crdt PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(tst_crdt PRIVATE core)

add_test(NAME crdt COMMAND tst_crdt)

# --- Payload codec tests (pure C++, no Qt) ---
add_executable(tst_payload
    tst_payload.cpp
)

target_include_directories(tst_payload PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(tst_payload PRIVATE core)

add_test(NAME payload COMMAND tst_payload)

# --- Relay / Nostr transport tests ---
qt_add_executable(tst_relay
    tst_relay.cpp
)

target_include_directories(tst_relay PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(tst_relay PRIVATE
    net
    Qt6::Test
    Qt6::WebSockets
)

add_test(NAME relay COMMAND tst_relay)

# --- ItemModel tests (QAbstractListModel + CRDT-aware slots) ---
qt_add_executable(tst_itemmodel
    tst_itemmodel.cpp
)

target_include_directories(tst_itemmodel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(tst_itemmodel PRIVATE
    applib
    store
    Qt6::Test
    Qt6::Sql
)

add_test(NAME itemmodel COMMAND tst_itemmodel)

# --- Crypto tests (XChaCha20-Poly1305, BIP340 Schnorr, NIP-01) ---
qt_add_executable(tst_crypto
    tst_crypto.cpp
)

target_include_directories(tst_crypto PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(tst_crypto PRIVATE
    cryptolayer
    core
    net
    Qt6::Test
    Qt6::Core
)

add_test(NAME crypto COMMAND tst_crypto)

# --- Pairing URI tests (pure C++, no Qt) ---
add_executable(tst_pairing
    tst_pairing.cpp
)

target_include_directories(tst_pairing PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(tst_pairing PRIVATE core)

add_test(NAME pairing COMMAND tst_pairing)
