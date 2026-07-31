# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0

set(CMAKE_AUTOMOC ON)

# Pure (no D-Bus) capability → UI-state classification. Links only the agent
# client's pure `AgentCapabilities` header + the classifier source.
add_executable(CardStateModelTest
    CardStateModelTest.cpp
    ${CMAKE_SOURCE_DIR}/components/plasmoid/src/CardStateModel.cpp
)
target_include_directories(CardStateModelTest PRIVATE
    ${CMAKE_SOURCE_DIR}/components/plasmoid/src
)
target_link_libraries(CardStateModelTest
    PRIVATE GTest::gtest_main Qt6::Core librekde-agentclient
)
target_compile_features(CardStateModelTest PRIVATE cxx_std_23)
add_test(NAME CardStateModelTest COMMAND CardStateModelTest)
set_tests_properties(CardStateModelTest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en"
)

# SmartCardHandler driven by the FakeAgent over a private session bus (reuses
# the agent-client D-Bus harness). Runs under dbus-run-session.
ecm_qt_declare_logging_category(PlasmoidTestLogging_SRCS
    HEADER plasmoid_log_categories.h
    IDENTIFIER LibreKDE::Plasmoid::Logging
    CATEGORY_NAME rs.librescrs.librekde.plasmoid
    EXPORT LibreKDE
    DESCRIPTION "LibreKDE Plasma 6 plasmoid"
)

add_executable(SmartCardHandlerTest
    SmartCardHandlerTest.cpp
    ${CMAKE_SOURCE_DIR}/components/plasmoid/src/CardStateModel.cpp
    ${CMAKE_SOURCE_DIR}/components/plasmoid/src/SmartCardHandler.cpp
    ${CMAKE_SOURCE_DIR}/components/plasmoid/src/CardPhotoStore.cpp
    ${CMAKE_SOURCE_DIR}/components/plasmoid/src/CardPhotoProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PlasmoidTestMain.cpp
    ${PlasmoidTestLogging_SRCS}
)
target_include_directories(SmartCardHandlerTest PRIVATE
    ${CMAKE_SOURCE_DIR}/components/plasmoid/src
    ${CMAKE_CURRENT_BINARY_DIR}
    # CardPhotoProvider.cpp includes the export header GenerateExportHeader
    # emits into the plasmoid's binary dir (librekde_plasmoid_export.h).
    ${CMAKE_BINARY_DIR}/components/plasmoid
)
target_link_libraries(SmartCardHandlerTest
    PRIVATE GTest::gtest librekde-agentclient librekde-test-support librekde-signing
            Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::DBus Qt6::Test
            KF6::I18n KF6::CoreAddons KF6::KIOGui KF6::Service LibreKDELogging
)
target_compile_features(SmartCardHandlerTest PRIVATE cxx_std_23)
add_test(NAME SmartCardHandlerTest COMMAND dbus-run-session -- $<TARGET_FILE:SmartCardHandlerTest>)
set_tests_properties(SmartCardHandlerTest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en"
)
