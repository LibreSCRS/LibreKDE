# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# card:/ KIO worker (read-only virtual filesystem over org.librescrs.Agent). A
# thin agent client: NO card I/O, NO secrets, NO LibreMiddleware. The pure
# layout/render core (CardTree + renderers) is KIO-free and D-Bus-free so it is
# unit-tested against an in-memory fake; AgentCardDataSource is the only I/O
# seam (over librekde-agentclient); CardWorker is the KIO::WorkerBase glue.

set(CMAKE_AUTOMOC ON)

# Pure core + the agent-backed data source, shared by the worker and the tests.
# CardTree + CardRenderers are pure (no KIO, no D-Bus). AgentCardDataSource adds
# the QtDBus/agentclient dependency but stays KIO-free (driven by the worker's
# own event loop via a scoped QEventLoop).
add_library(librekde-kio-core STATIC
    CardDataSource.h
    CardTree.h
    CardTree.cpp
    CardRenderers.h
    CardRenderers.cpp
    AgentCardDataSource.h
    AgentCardDataSource.cpp
    CardWorkerLogic.h
    CardWorkerLogic.cpp
)
target_include_directories(librekde-kio-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(librekde-kio-core
    PUBLIC  Qt6::Core KF6::KIOCore
    PRIVATE Qt6::DBus KF6::I18n librekde-agentclient
)
target_compile_features(librekde-kio-core PUBLIC cxx_std_23)
# CardWorkerLogic's bare i18n()/i18nc() (cert-folder names, error copy) must
# resolve to "librekde" — a dlopened worker .so does not inherit an app domain.
target_compile_definitions(librekde-kio-core PRIVATE TRANSLATION_DOMAIN="librekde")
set_target_properties(librekde-kio-core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)

# The KIO worker plugin: a kcoreaddons plugin under kf6/kio that KIO dlopens as
# its own process for the `card` protocol.
kcoreaddons_add_plugin(card
    INSTALL_NAMESPACE "kf6/kio"
    SOURCES
        CardWorker.h
        CardWorker.cpp
)
target_link_libraries(card
    PRIVATE
        Qt6::Core Qt6::DBus
        KF6::KIOCore KF6::I18n KF6::CoreAddons
        librekde-agentclient
        librekde-kio-core
)
target_compile_features(card PRIVATE cxx_std_23)
# A KIO worker .so is dlopened into its own process and does NOT inherit an app
# translation domain — pin the worker's bare i18n()/ErrorText copy to "librekde",
# same rationale as the Purpose plugin.
target_compile_definitions(card PRIVATE TRANSLATION_DOMAIN="librekde")

# KF6 KIO no longer reads a separate .protocol file: the protocol descriptor is
# the plugin's embedded JSON (card.json, under "KDE-KIO-Protocols"), compiled in
# via Q_PLUGIN_METADATA(... FILE "card.json"). No extra install rule needed.

# Regression guard: KIO 6's out-of-process kioworker helper dlopens the worker
# lib and resolves a `kdemain` symbol; without it the worker is rejected ("Could
# not find kdemain") and card:/ hangs (see CardWorker.cpp). Fail the build if a
# future change drops that entry point.
find_program(LIBREKDE_NM_EXECUTABLE nm)
if(LIBREKDE_NM_EXECUTABLE)
    add_custom_command(TARGET card POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DNM=${LIBREKDE_NM_EXECUTABLE}
            -DWORKER=$<TARGET_FILE:card>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckKdemainExport.cmake
        VERBATIM
        COMMENT "card://: verifying the worker exports kdemain")
else()
    message(WARNING "nm not found: skipping the card:// kdemain-export guard")
endif()
