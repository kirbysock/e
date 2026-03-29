cmake_minimum_required(VERSION 3.21)

project(ChattingApp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Sql)

qt_standard_project_setup()

function(add_qt_runtime_deploy target_name)
    if(WIN32)
        get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
        if(NOT QT_CORE_DLL)
            get_target_property(QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
        endif()

        if(QT_CORE_DLL)
            get_filename_component(QT_BIN_DIR "${QT_CORE_DLL}" DIRECTORY)
            get_filename_component(QT_ROOT_DIR "${QT_BIN_DIR}" DIRECTORY)

            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}"
                    -DQT_BIN_DIR=${QT_BIN_DIR}
                    -DQT_PLUGIN_DIR=${QT_ROOT_DIR}/plugins
                    -DAPP_DIR=$<TARGET_FILE_DIR:${target_name}>
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployQtRuntime.cmake"
                VERBATIM
            )
        endif()
    endif()
endfunction()

qt_add_executable(chatting_app
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/remoteapiclient.cpp
    src/remoteapiclient.h
    src/serverconfig.h
    src/databasemanager.cpp
    src/databasemanager.h
)

target_include_directories(chatting_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(chatting_app PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
)

set_target_properties(chatting_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
)

add_qt_runtime_deploy(chatting_app)
