// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <QByteArray>
#include <QDBusUnixFileDescriptor>

#include <sys/mman.h>
#include <sys/stat.h>

/// @file
/// @brief The audited reader for BOUNDED sealed-memfd payloads the agent hands
///        over a `QDBusUnixFileDescriptor` — small, fully-buffered data such as
///        `Photo1.Result`. It mmaps the whole fd into a `QByteArray`, so every
///        surface reading a bounded payload (the `card:/` worker, the plasmoid)
///        shares this single implementation rather than re-spelling sealed-fd +
///        mmap + PII handling per call site.
///
/// It is deliberately NOT used for arbitrary-size streamed artifacts (e.g.
/// `Sign1.Result`, which may be a large signed PDF): those are copied through a
/// bounded read-loop straight into a `QSaveFile` by the consumer
/// (`purpose/SignJob`), never mmapped whole into memory.
///
/// Qt-only and LibreMiddleware-free, matching the rest of
/// `librekde-agentclient`. NEVER logs the bytes — the payload is PII.

namespace LibreKDE {

/// @brief mmap-read the whole sealed memfd behind @p sealed into a QByteArray.
///        Returns an empty array on an invalid fd, a non-positive size, or an
///        mmap failure. The mapping is released before returning.
[[nodiscard]] inline QByteArray readSealedFd(const QDBusUnixFileDescriptor& sealed)
{
    if (!sealed.isValid()) {
        return {};
    }
    const int fd = sealed.fileDescriptor();
    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
        return {};
    }
    const auto size = static_cast<size_t>(st.st_size);
    void* map = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        return {};
    }
    QByteArray out(static_cast<const char*>(map), static_cast<qsizetype>(size));
    ::munmap(map, size);
    return out;
}

} // namespace LibreKDE
