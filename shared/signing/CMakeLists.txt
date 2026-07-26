# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# librekde-signing — the shared agent-driven signing core. The Purpose Share
# plugin AND the Plasma plasmoid both link this and ONLY this for signing, so
# there is exactly one signing path. A thin client of
# org.librescrs.Agent: NO card I/O, NO secrets, NO LibreMiddleware — the agent's
# prompter collects the PIN/CAN.

set(CMAKE_AUTOMOC ON)

# The shared cert-chooser / overwrite-confirmer seams are modal QtWidgets
# dialogs (QInputDialog / QMessageBox), so this shared layer links Qt6::Widgets.
# It is a PRIVATE dep (encapsulated in SignSeams.cpp): consumers link
# librekde-signing and get the seams without pulling Widgets includes.
#
# Qt6::Widgets is found at ROOT scope (root CMakeLists.txt, before
# add_subdirectory(shared)), NEVER here: a find_package in this subdirectory
# would scope the imported Qt6::Widgets target to this subtree only
# (QT_PROMOTE_TO_GLOBAL_TARGETS is unset by default), so every SIBLING consumer
# that transitively links librekde-signing — the Purpose plugin, the plasmoid,
# and their tests — would fail to resolve Qt6::Widgets at CMake generate time
# ("links to target Qt6::Widgets but the target was not found").

ecm_qt_declare_logging_category(SigningLogging_SRCS
    HEADER signing_log_categories.h
    IDENTIFIER LibreKDE::Signing::Logging
    CATEGORY_NAME rs.librescrs.librekde.signing
    EXPORT LibreKDE
    DESCRIPTION "LibreKDE shared signing core"
)

add_library(librekde-signing STATIC
    MimeFormatMap.h
    MimeFormatMap.cpp
    CertSelector.h
    SignJob.h
    SignJob.cpp
    SignSeams.h
    SignSeams.cpp
    ${SigningLogging_SRCS}
)
target_include_directories(librekde-signing
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
)
# librekde-agentclient is PUBLIC: SignJob.h -> CertSelector.h -> AgentOperation.h
# exposes agentclient types (CertificateList) in this lib's public interface.
target_link_libraries(librekde-signing
    PUBLIC  Qt6::Core librekde-agentclient
    PRIVATE Qt6::DBus KF6::I18n Qt6::Widgets LibreKDELogging
)
target_compile_features(librekde-signing PUBLIC cxx_std_23)
set_target_properties(librekde-signing PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)
# SignJob's / SignSeams' bare i18nc() calls live in this static lib, dlopened into
# arbitrary hosts (the Purpose plugin) — so the catalog they resolve to must be
# pinned to "librekde" at compile time, never inherited from the host's domain.
target_compile_definitions(librekde-signing PRIVATE TRANSLATION_DOMAIN="librekde")
