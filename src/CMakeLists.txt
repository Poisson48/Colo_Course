qt_add_executable(colocourse
    app/main.cpp
)

qt_add_qml_module(colocourse
    URI ColoCourse
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_link_libraries(colocourse PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Sql
    Qt6::WebSockets
)
