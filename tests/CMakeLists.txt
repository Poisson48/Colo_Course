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
