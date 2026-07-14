# Find libsodium and libsecp256k1 via pkg-config.
# IMPORTED_TARGET: les cibles portent aussi les -L, indispensable en cross-build
# (le préfixe Android n'est pas dans les chemins par défaut du linker).
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(SECP256K1 REQUIRED IMPORTED_TARGET libsecp256k1)

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
target_link_libraries(cryptolayer PUBLIC
    Qt6::Core
    net
    PkgConfig::SODIUM
    PkgConfig::SECP256K1
)

# --- app library (AppController, ItemModel, QrImageProvider, SyncEngine) ---
add_library(applib STATIC
    app/appcontroller.cpp
    app/itemmodel.cpp
    app/platform.cpp
    app/qrimageprovider.cpp
    app/syncengine.cpp
)

target_include_directories(applib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(applib PUBLIC
    core
    cryptolayer
    net
    store
    qrcodegen
    Qt6::Core
    Qt6::Quick
    Qt6::Sql
    Qt6::WebSockets
    PkgConfig::SODIUM
    PkgConfig::SECP256K1
)

if(TARGET Qt6::Widgets)
    target_link_libraries(applib PUBLIC Qt6::Widgets)
endif()

# --- main application ---
qt_add_executable(colocourse
    app/main.cpp
)

# Sans ce marqueur, `pragma Singleton` ne suffit pas : le qmldir généré ne déclare
# pas Theme comme singleton et toutes ses couleurs arrivent en `undefined`.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(colocourse
    URI ColoCourse
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/ListsPage.qml
        qml/ListPage.qml
        qml/ShareSheet.qml
        qml/ColoDialog.qml
        qml/ColoTextField.qml
        qml/Theme.qml
)

target_link_libraries(colocourse PRIVATE
    applib
    net
    cryptolayer
    store
    qrcodegen
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Sql
    Qt6::WebSockets
    PkgConfig::SODIUM
    PkgConfig::SECP256K1
)

# Paquet Android : manifest (permissions, label, icône), ressources et le Java
# le Java du pont plateforme (android/src/org/colocourse/app/Platform.java).
set_property(TARGET colocourse PROPERTY
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android")
# targetSdk 34 volontairement, pas 35 : à partir d'Android 15, cibler 35 impose
# l'edge-to-edge, et Qt 6.8 ne gère pas les insets — l'UI passerait sous la barre
# de statut et sous la barre de navigation.
set_property(TARGET colocourse PROPERTY QT_ANDROID_TARGET_SDK_VERSION 34)

# Version de l'APK. scripts/build-android.sh passe ces valeurs et aligne le manifest
# dessus ; le workflow de release les dérive du tag git.
set(COLO_VERSION_NAME "0.1.0" CACHE STRING "android:versionName")
set(COLO_VERSION_CODE "1"     CACHE STRING "android:versionCode")
set_property(TARGET colocourse PROPERTY QT_ANDROID_VERSION_NAME "${COLO_VERSION_NAME}")
set_property(TARGET colocourse PROPERTY QT_ANDROID_VERSION_CODE "${COLO_VERSION_CODE}")

# Android n'embarque pas OpenSSL : sans ces .so, QSslSocket est muet et les
# connexions wss:// aux relais échouent silencieusement.
if(ANDROID)
    include(FetchContent)
    FetchContent_Declare(android_openssl
        GIT_REPOSITORY https://github.com/KDAB/android_openssl.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(android_openssl)
    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
    add_android_openssl_libraries(colocourse)
endif()
