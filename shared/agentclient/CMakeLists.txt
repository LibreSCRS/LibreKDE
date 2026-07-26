# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# librekde-agentclient — shared QtDBus client for org.librescrs.Agent. Every
# LibreKDE surface (plasmoid, Purpose, …) consumes this and ONLY this for card
# access. Deliberately LibreMiddleware-free: a thin D-Bus client
# holds no card connection, no secrets, links no LM target.

add_library(librekde-agentclient STATIC
    AgentDBus.h
    CappedCall.h
    CappedCall.cpp
    AgentCapabilities.h
    SealedFd.h
    ErrorText.h
    ErrorText.cpp
    CredentialTypes.h
    CredentialTypes.cpp
    CredentialText.h
    CredentialText.cpp
    AgentOperation.h
    AgentOperation.cpp
    IdentityRows.h
    IdentityRows.cpp
    AgentReader.h
    AgentReader.cpp
    AgentCard.h
    AgentCard.cpp
    AgentClient.h
    AgentClient.cpp
    SharedAgentClient.h
    SharedAgentClient.cpp
)
target_include_directories(librekde-agentclient PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(librekde-agentclient
    PUBLIC  Qt6::Core Qt6::DBus
    PRIVATE KF6::I18n
)
target_compile_features(librekde-agentclient PUBLIC cxx_std_23)
set_target_properties(librekde-agentclient PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)
