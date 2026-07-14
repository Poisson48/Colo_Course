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
