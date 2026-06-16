cmake_minimum_required(VERSION 3.20)
project(Lindora VERSION 1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INSTALL_PREFIX /usr)

find_package(Qt6 REQUIRED COMPONENTS
    Core Widgets WebEngineWidgets Svg DBus
)

add_executable(lindora main.cpp)

target_link_libraries(lindora PRIVATE
    Qt6::Core Qt6::Widgets Qt6::WebEngineWidgets Qt6::Svg Qt6::DBus
)

set_target_properties(lindora PROPERTIES OUTPUT_NAME lindora)

install(TARGETS lindora DESTINATION bin)
install(FILES lindora-native.desktop DESTINATION share/applications)
install(FILES Lindora.svg DESTINATION share/icons/hicolor/scalable/apps)
