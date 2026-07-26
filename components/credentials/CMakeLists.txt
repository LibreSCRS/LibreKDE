# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0

set(CMAKE_AUTOMOC ON)

# Backing static library (linked by the exe AND the tests, so the controller +
# model stay unit-testable without the app), mirroring the plasmoid's backing-lib
# pattern.
qt_add_library(credentials-core STATIC
    src/CredentialController.h src/CredentialController.cpp
    src/CredentialModel.h src/CredentialModel.cpp
)
qt_add_qml_module(credentials-core
    URI org.librescrs.credentials
    VERSION 1.0
    # Embed the module under the modern default QML resource root so the engine's
    # built-in `qrc:/qt/qml` import path resolves `import org.librescrs.credentials`
    # (and its `Main` type) from the statically-linked resource. Without this the
    # module lands at `qrc:/org/...`, which is NOT on the import path, and
    # loadFromModule reports "no type named Main".
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        qml/Main.qml
        qml/Dashboard.qml
        qml/CredentialDelegate.qml
        qml/StateMessage.qml
        qml/ResultBanner.qml
        qml/UnblockConfirmSheet.qml
)
target_compile_definitions(credentials-core PUBLIC
    TRANSLATION_DOMAIN="librekde" LIBREKDE_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(credentials-core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::DBus
    KF6::Kirigami KF6::I18n KF6::CoreAddons KF6::DBusAddons
    librekde-agentclient)
target_compile_features(credentials-core PUBLIC cxx_std_23)
target_include_directories(credentials-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The window process. QApplication (not QGuiApplication) is required: the
# org.kde.desktop QtQuick Controls style is QtWidgets-backed, so the exe links
# Qt6::Widgets.
qt_add_executable(librescrs-credentials-kde src/main.cpp)
target_link_libraries(librescrs-credentials-kde PRIVATE
    credentials-core credentials-coreplugin Qt6::Widgets)
target_compile_features(librescrs-credentials-kde PRIVATE cxx_std_23)
install(TARGETS librescrs-credentials-kde ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES org.librescrs.credentials.desktop DESTINATION ${KDE_INSTALL_APPDIR})
