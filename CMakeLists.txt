cmake_minimum_required(VERSION 3.21)
project(MacroFlow VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(MMR_BUILD_APP "Build the Qt/Windows application" ${WIN32})
option(MMR_BUILD_TESTS "Build tests" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

# ---- pure C++ core (no Qt, no Windows headers) ---------------------------------------------
add_library(mmr_core STATIC
    core/MacroTypes.cpp core/EventBuffer.cpp core/RecordingEngine.cpp
    core/PlaybackEngine.cpp core/ShortcutTypes.cpp core/MarkerBuilder.cpp)
target_include_directories(mmr_core PUBLIC ${CMAKE_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(mmr_core PUBLIC Threads::Threads)

if(MMR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Sql Svg)

    add_library(mmr_storage STATIC storage/DatabaseManager.cpp storage/MacroRepository.cpp)
    target_link_libraries(mmr_storage PUBLIC mmr_core Qt6::Core Qt6::Sql)

    add_library(mmr_settings STATIC settings/SettingsManager.cpp settings/ShortcutManager.cpp)
    target_link_libraries(mmr_settings PUBLIC mmr_core Qt6::Core)

    if(NOT WIN32)
        message(WARNING "The full application is Windows-only; only storage/settings libraries and tests are built.")
    else()
        add_executable(MacroFlow WIN32
            app/main.cpp app/Application.cpp
            core/MacroController.cpp core/AutoClicker.cpp
            platform/windows/InputHook.cpp platform/windows/WindowProbe.cpp platform/windows/MouseInput.cpp
            platform/windows/GlobalHotkeys.cpp platform/windows/DisplayMonitor.cpp
            platform/windows/AutoStart.cpp platform/windows/TaskbarBadge.cpp
            ui/MainWindow.cpp ui/RecorderWidget.cpp ui/RecordsWidget.cpp ui/SettingsWidget.cpp
            ui/RecordItemWidget.cpp ui/StatusIndicator.cpp ui/ShortcutEdit.cpp ui/ClickOverlay.cpp
            ui/ThemedDialog.cpp ui/AutoClickerWidget.cpp ui/ClickPointPicker.cpp
            resources/resources.qrc resources/app.rc)
        target_include_directories(MacroFlow PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/app)
        target_link_libraries(MacroFlow PRIVATE mmr_storage mmr_settings
            Qt6::Widgets Qt6::Svg Qt6::Sql user32 winmm shcore dwmapi ole32)
        set_target_properties(MacroFlow PROPERTIES WIN32_EXECUTABLE ON)

        # Make MacroFlow the default startup target (Visual Studio)
        set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT MacroFlow)

        # windeployqt: copies Qt DLLs + platform/sqlite/svg plugins next to the exe after every build
        get_target_property(_mmr_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_mmr_qtbin "${_mmr_qmake}" DIRECTORY)
        find_program(MMR_WINDEPLOYQT windeployqt HINTS "${_mmr_qtbin}")
        if(MMR_WINDEPLOYQT)
            add_custom_command(TARGET MacroFlow POST_BUILD
                COMMAND "${MMR_WINDEPLOYQT}" --no-translations --no-opengl-sw "$<TARGET_FILE:MacroFlow>"
                COMMENT "Running windeployqt")
        else()
            message(WARNING "windeployqt not found; run scripts/deploy.ps1 manually.")
        endif()
    endif()
endif()

if(MMR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
