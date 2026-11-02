// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once
#include <LibreSCRS/AgentClient/AgentClient.h>
#include <LibreSCRS/AgentClient/AgentReader.h>

#include <QString>

/// @file
/// @brief Host-side reader helpers over the agent client's deterministic
///        `readers()` view: the finder every unbound surface uses, and the
///        label every surface shows a reader under. Selection policy and
///        wording are deliberately NOT the client library's to carry, so the
///        hosts share one copy here instead of each component re-adding its own.

namespace LibreKDE::Readers {

/// @brief The first reader (in the client's id-sorted order) holding a
///        resolvable card — the "obvious card" every unbound surface manages.
[[nodiscard]] LibreSCRS::AgentClient::AgentReader* firstReaderWithCard(LibreSCRS::AgentClient::AgentClient& client);

/// @brief The label a reader is shown under: the agent's model for it, with
///        the interface the agent names said in the holder's language
///        ("— contact" / "— contactless"; nothing for Unknown). The agent
///        already keeps every model distinct, so nothing here parses the raw
///        PC/SC name or makes a label unique. The raw name stays the binding,
///        selection and URL key everywhere; this only decides what is SHOWN.
[[nodiscard]] QString readerLabel(const QString& model, LibreSCRS::AgentClient::ReaderInterface iface);

/// @brief `readerLabel()` over what the agent currently says about @p reader.
///        The agent relabels a reader when another arrives or leaves beside
///        it (`AgentReader::changed`), so a surface asks again rather than
///        keeping the answer.
[[nodiscard]] QString readerLabel(const LibreSCRS::AgentClient::AgentReader& reader);

} // namespace LibreKDE::Readers
