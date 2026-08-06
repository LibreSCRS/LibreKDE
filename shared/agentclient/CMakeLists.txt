# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# librekde-agentclient — the host-side text layer over the shared agent client
# library. Card access itself, the transport and every wire value type belong to
# LibreAgent::ClientQt; what is left here is the one thing that library will not
# do, because it renders no text at all: turn its error taxonomy, its credential
# vocabulary and the frozen identity label keys into localized, user-facing
# strings. Every LibreKDE surface (plasmoid, credential window, card:/ worker,
# Purpose signing) draws its copy from here, so the wording is decided once.
# Alongside the text layer live the small host-side helpers the components
# would otherwise duplicate verbatim: the first-reader-with-card finder
# (Readers) and the rich-text neutralizer (DisplayText).
#
# Deliberately LibreMiddleware-free, as before: this links no card core.

add_library(librekde-agentclient STATIC
    ErrorText.h
    ErrorText.cpp
    CredentialText.h
    CredentialText.cpp
    DisplayText.h
    DisplayText.cpp
    IdentityRows.h
    IdentityRows.cpp
    Readers.h
    Readers.cpp
)
target_include_directories(librekde-agentclient PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# LibreAgent::ClientQt is PUBLIC because all three public headers here name that
# library's types in their own signatures: ErrorText takes its ErrorCode and
# CallError, CredentialText its credential vocabulary, IdentityRows its
# IdentityRow. A consumer of these headers therefore needs its headers too.
#
# Those types are consumed rather than mirrored. A hand-kept copy of a wire
# contract has to be diffed against the contract by a human on every change, and
# this repo has no build edge to the agent to catch a missed one — so the copy is
# not worth its upkeep when the client library already publishes the type.
target_link_libraries(librekde-agentclient
    PUBLIC  Qt6::Core LibreAgent::ClientQt
    # Qt6::Gui only for Qt::mightBeRichText (DisplayText's rich-text probe) —
    # private, since no public header here names a QtGui type.
    PRIVATE KF6::I18n Qt6::Gui
)
target_compile_features(librekde-agentclient PUBLIC cxx_std_23)
# No AUTOMOC: nothing here derives from QObject any more — the D-Bus proxies that
# did are the client library's. PIC stays, because a plugin (the card:/ worker,
# the Purpose shim) links this static library into a shared object.
set_target_properties(librekde-agentclient PROPERTIES POSITION_INDEPENDENT_CODE ON)
