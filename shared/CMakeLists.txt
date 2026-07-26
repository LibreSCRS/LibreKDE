# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# LibreKDE shared layer — used by every component. Deliberately thin:
# logging categories and the agent D-Bus client. NOT a "framework" — only
# the bits that would be cut-and-pasted across components if they weren't
# here.
#
# Under the agent architecture the client holds no card session and
# collects no secrets: the agent's prompter owns CAN/MRZ/PIN entry, so the
# former `session/` (CardSessionContext) and `input/` (SecureInputDialog
# family) layers were removed. The `i18n/` LocalizedText→QString adapter
# (an LM type bridge) went with them — the client now renders agent-supplied
# `msgKey`/`msgFallback` strings via `agentclient/ErrorText`, keeping the
# whole LibreKDE closure LibreMiddleware-free.

add_subdirectory(logging)
add_subdirectory(agentclient)

# The shared signing core is only needed when a surface that signs is built.
if(LIBREKDE_BUILD_PLASMOID OR LIBREKDE_BUILD_PURPOSE)
    add_subdirectory(signing)
endif()
