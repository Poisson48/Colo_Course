# Find libsodium and libsecp256k1 via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)
pkg_check_modules(SECP256K1 REQUIRED libsecp256k1)

# --- qrcodegen static library (vendored from Nayuki) ---
add_library(qrcodegen STATIC
    ${CMAKE_SOURCE_DIR}/third_party/qrcodegen/qrcodegen.cpp
)
target_include_directories(qrcodegen PUBLIC ${CMAKE_SOURCE_DIR}/third_party/qrcodegen)

# --- net static library (QWebSocket, Nostr NIP-01) ---
add_library(net STATIC
    net/nostr.cpp
    net/relayclient.cpp
    net/relaypool.cpp
)

target_include_directories(net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(net PUBLIC
    Qt6::Core
    Qt6::WebSockets
)

# --- core static library (pure C++/STL, no Qt) ---
add_library(core STATIC
    core/crdt.cpp
    core/payload.cpp
    core/pairing.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
)

# core is pure STL: no Qt linkage.

# --- store static library ---
add_library(store STATIC
    store/database.cpp
)

target_include_directories(store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(store PUBLIC
    Qt6::Core
    Qt6::Sql
)

# --- cryptolayer static library (net/crypto.cpp) ---
add_library(cryptolayer STATIC
    net/crypto.cpp
)

target_include_directories(cryptolayer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(cryptolayer PRIVATE
    ${SODIUM_INCLUDE_DIRS}
    ${SECP256K1_INCLUDE_DIRS}
)
target_link_libraries(cryptolayer PUBLIC
    Qt6::Core
    net
    ${SODIUM_LIBRARIES}
    ${SECP256K1_LIBRARIES}
)

# --- app library (AppController, ItemModel, QrImageProvider) ---
add_library(applib STATIC
    app/appcontroller.cpp
    app/itemmodel.cpp
    app/qrimageprovider.cpp
)

target_include_directories(applib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(applib PUBLIC
    core
    cryptolayer
    store
    qrcodegen
    Qt6::Core
    Qt6::Quick
    Qt6::Sql
    ${SODIUM_LIBRARIES}
    ${SECP256K1_LIBRARIES}
)

# --- main application ---
qt_add_executable(colocourse
    app/main.cpp
)

qt_add_qml_module(colocourse
    URI ColoCourse
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/ListsPage.qml
        qml/ListPage.qml
)

target_link_libraries(colocourse PRIVATE
    applib
    net
    cryptolayer
    store
    qrcodegen
    Qt6::Core
    Qt6::Quick
    Qt6::Sql
    Qt6::WebSockets
    ${SODIUM_LIBRARIES}
    ${SECP256K1_LIBRARIES}
)
