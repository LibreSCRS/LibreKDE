# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# Identity-render fence — a source gate for the two ways an identity view can
# start showing a field twice, or showing it in an order it invented. Neither is
# reachable from a unit test of the code that exists today, because both are
# introduced by WRITING something rather than by a value flowing somewhere; a
# grep is the only thing that fails when they come back.
#
#   1. Progressive group rendering. `AgentOperation` offers a `groupReady()`
#      signal that hands out identity groups as they arrive. Those are HINTS —
#      the client library's own header says so — and nothing in the wire
#      contract fixes their order or promises the set is complete. Every
#      LibreKDE surface therefore renders from the FINAL identity result and
#      only from it. A surface that connects `groupReady()` to its model is
#      building an order-dependent view of an unordered stream: the defect
#      shows up as a group heading printed twice, or fields under the wrong
#      heading, on whichever card happens to emit in a different order.
#
#   2. The plasmoid's views bound to the whole model. `identitySummary` is a
#      curated SUBSET of `identityFields`, and the popup renders the summary
#      and (once expanded) the detail list at the same time. Any QML sight of
#      `identityFields` therefore prints summarised rows a second time — the
#      "Card Type twice" report — whether it is bound directly on a `model:`
#      line, laundered through an intermediate property, or wrapped onto the
#      next line. The model publishes `identityDetails` (fields MINUS summary)
#      precisely so the two lists partition the model; the C++ side of that
#      partition is pinned by
#      SmartCardHandlerSummary.SummaryAndDetailsPartitionTheFields, and this
#      gate pins the QML side, which no test in this repo can reach (the
#      plasmoid package UI is lint-checked, never instantiated).
#
# Both pattern families apply to BOTH languages: `onGroupReady` is the QML
# handler spelling, `SIGNAL(groupReady(...))` is the string-based Qt connect
# that predates pointer-to-member syntax and still compiles, and QML under any
# components/ subtree is in scope — not just the one directory the popup's
# main view happens to live in.
#
# Run standalone with:
#   cmake -DLIBREKDE_SOURCE_DIR=<checkout> -P tools/identity-render-fence.cmake
# Registered as the `IdentityRenderFence` ctest case, so it runs in every local
# `ctest` and in CI's test step.

if(NOT DEFINED LIBREKDE_SOURCE_DIR)
    message(FATAL_ERROR "LIBREKDE_SOURCE_DIR is not set — pass -DLIBREKDE_SOURCE_DIR=<checkout>")
endif()
# A wrong or empty path would make every scan below match nothing and the gate
# would pass without having read a byte. Prove the checkout is there first.
foreach(_required components shared components/plasmoid/package/contents/ui)
    if(NOT IS_DIRECTORY "${LIBREKDE_SOURCE_DIR}/${_required}")
        message(FATAL_ERROR "LIBREKDE_SOURCE_DIR='${LIBREKDE_SOURCE_DIR}' has no ${_required}/ — not a LibreKDE checkout")
    endif()
endforeach()

set(_violations "")

# The groupReady ban, in every spelling a subscription can take: the
# pointer-to-member form (`&Op::groupReady`, including through a type alias),
# the QML/old-connect handler form (`onGroupReady:`), and the string-based
# SIGNAL macro form. Prose may name the signal freely — these are the
# spellings a CONNECTION uses.
set(_group_ready_pattern "::groupReady|onGroupReady|SIGNAL\\(.*groupReady")
# The whole-model ban: any QML sight of identityFields at all. Views consume
# identitySummary / identityDetails; the full list is not theirs to render,
# and an intermediate `property var x: smartCard.identityFields` is the same
# defect wearing a coat.
set(_whole_model_pattern "identityFields")

# _strip_comments: drop the `//` tail of a line so prose may keep naming the
# things it warns about — a comment that says "NOT identityFields" is the
# fence's ally, not a violation. Line comments only; QML block comments around
# these identifiers do not occur in this tree and a false NEGATIVE from one
# would still be caught the moment the code itself appeared on any line.
function(_scan_files _pattern _files _advice _strip_comments)
    foreach(_file IN LISTS _files)
        file(STRINGS "${_file}" _lines)
        set(_lineno 0)
        foreach(_line IN LISTS _lines)
            math(EXPR _lineno "${_lineno} + 1")
            if(_strip_comments)
                string(REGEX REPLACE "//.*$" "" _line "${_line}")
            endif()
            if(_line MATCHES "${_pattern}")
                file(RELATIVE_PATH _rel "${LIBREKDE_SOURCE_DIR}" "${_file}")
                list(APPEND _violations "${_rel}:${_lineno}: ${_advice}")
            endif()
        endforeach()
    endforeach()
    set(_violations "${_violations}" PARENT_SCOPE)
endfunction()

# --- 1. No progressive-group rendering in the live consumers -----------------
# Sources only: a test MAY drive groupReady() deliberately, and this gate is
# about what the shipped surfaces subscribe to.
file(GLOB_RECURSE _cxx_sources
    "${LIBREKDE_SOURCE_DIR}/components/*.cpp"
    "${LIBREKDE_SOURCE_DIR}/components/*.h"
    "${LIBREKDE_SOURCE_DIR}/shared/*.cpp"
    "${LIBREKDE_SOURCE_DIR}/shared/*.h")
list(LENGTH _cxx_sources _cxx_count)
if(_cxx_count LESS 20)
    message(FATAL_ERROR "scanned only ${_cxx_count} C++ sources under components/ and shared/ — the glob is wrong, "
                        "and a gate that reads nothing passes on anything")
endif()

# --- 2. Every QML file under components/, not one directory ------------------
file(GLOB_RECURSE _qml_sources "${LIBREKDE_SOURCE_DIR}/components/*.qml")
list(LENGTH _qml_sources _qml_count)
if(_qml_count LESS 5)
    message(FATAL_ERROR "scanned only ${_qml_count} QML files under components/ — the glob is wrong")
endif()
if(NOT EXISTS "${LIBREKDE_SOURCE_DIR}/components/plasmoid/package/contents/ui/IdentityView.qml")
    message(FATAL_ERROR "IdentityView.qml is gone — this gate no longer measures what it claims to")
endif()

set(_group_ready_advice
    "connects the progressive groupReady() stream. Identity views render from the final identityResult() only — the group stream is hints, with no ordering or completeness guarantee (see shared/agentclient/IdentityRows.h).")
set(_whole_model_advice
    "touches identityFields from QML. The summary is a SUBSET of that list and is rendered alongside it, so every summarised row would print twice — views consume identitySummary and identityDetails only.")

_scan_files("${_group_ready_pattern}" "${_cxx_sources}" "${_group_ready_advice}" FALSE)
_scan_files("${_group_ready_pattern}" "${_qml_sources}" "${_group_ready_advice}" TRUE)
_scan_files("${_whole_model_pattern}" "${_qml_sources}" "${_whole_model_advice}" TRUE)

# Known residual, stated rather than hidden: file(STRINGS) is line-based, so a
# binding wrapped as `model: root.smartCard` / newline / `.identityFields`
# defeats the whole-model scan's line regex. The bare-identifier pattern above
# closes the realistic half (the identifier itself cannot be split), and the
# groupReady patterns are single tokens for the same reason.

# --- verdict -----------------------------------------------------------------
if(_violations)
    list(JOIN _violations "\n  " _report)
    message(FATAL_ERROR "identity-render fence: ${_report}")
endif()

message(STATUS "identity-render fence: ${_cxx_count} C++ sources + ${_qml_count} QML files clean")
