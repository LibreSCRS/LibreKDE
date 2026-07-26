# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# Shared logging category — every component declares its own category
# under rs.librescrs.librekde.<component> via the same ECMQtDeclareLoggingCategory
# pattern; this target exposes the shared/ category used by the cross-cutting
# helpers (the agent D-Bus client).

ecm_qt_declare_logging_category(LibreKDELogging_SRCS
    HEADER librekde_log_categories.h
    IDENTIFIER LibreKDE::Logging::Shared
    CATEGORY_NAME rs.librescrs.librekde.shared
    EXPORT LibreKDE
    DESCRIPTION "LibreKDE shared layer"
)

add_library(LibreKDELogging STATIC ${LibreKDELogging_SRCS})
target_include_directories(LibreKDELogging PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(LibreKDELogging PUBLIC Qt6::Core)
target_compile_features(LibreKDELogging PUBLIC cxx_std_23)
set_target_properties(LibreKDELogging PROPERTIES POSITION_INDEPENDENT_CODE ON)
