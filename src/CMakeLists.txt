# --- core static library (pure C++/STL, no Qt) ---
add_library(core STATIC
    core/crdt.cpp
    core/payload.cpp
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

# --- main application ---
qt_add_executable(colocourse
    app/main.cpp
)

qt_add_qml_module(colocourse
    URI ColoCourse
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_link_libraries(colocourse PRIVATE
    store
    Qt6::Core
    Qt6::Quick
    Qt6::Sql
    Qt6::WebSockets
)
