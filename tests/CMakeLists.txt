# Pure-C++ tests: build and run anywhere (no Qt / Windows needed).
foreach(t EventSerializationTests MacroTimingTests ShortcutTests MarkerBuilderTests)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE mmr_core)
    target_include_directories(${t} PRIVATE ${CMAKE_SOURCE_DIR})
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# Qt-based tests (SQLite repository, settings, shortcut manager).
if(MMR_BUILD_APP)
    foreach(t MacroRepositoryTests SettingsTests)
        add_executable(${t} ${t}.cpp)
        target_link_libraries(${t} PRIVATE mmr_storage mmr_settings Qt6::Core Qt6::Sql)
        target_include_directories(${t} PRIVATE ${CMAKE_SOURCE_DIR})
        add_test(NAME ${t} COMMAND ${t})
    endforeach()
endif()
