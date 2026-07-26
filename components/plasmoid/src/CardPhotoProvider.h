// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CardPhotoStore.h"

#include "librekde_plasmoid_export.h" // LIBREKDE_PLASMOID_EXPORT

#include <QQuickImageProvider>

#include <memory>

class QQmlEngine;

namespace LibreKDE::Plasmoid {

/// @brief QML image provider serving card ID photos from the shared, slot-keyed
///        `CardPhotoStore`. Registered on the QML engine as `librekde`, so a QML
///        `Image { source: smartCard.cardPhotoUrl }` with the
///        `image://librekde/cardphoto/<slot>?<token>` URL resolves here; the
///        slot picks the requesting widget's store entry.
///
/// The provider co-owns the store (`shared_ptr`) with the per-widget
/// `SmartCardHandler`s, so it stays valid regardless of engine/handler teardown
/// order. `requestImage` may run on a non-GUI thread; the store is
/// mutex-guarded, satisfying the reentrancy contract.
class CardPhotoProvider : public QQuickImageProvider
{
public:
    explicit CardPhotoProvider(std::shared_ptr<CardPhotoStore> store);

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    /// @brief Pure, testable: parse the handler photo slot out of an image `id`
    ///        of the form `cardphoto/<slot>?<token>`. Returns 0 (an unallocated
    ///        slot) for malformed ids.
    [[nodiscard]] static quint64 slotFromImageId(const QString& id);

private:
    std::shared_ptr<CardPhotoStore> m_store;
};

/// @brief Register a `CardPhotoProvider` over the process-shared
///        `sharedCardPhotoStore()` on @p engine under the id `librekde`.
///        Exported so the QML engine-extension plugin can call it: the store
///        lookup then stays inside this library, avoiding a hidden-visibility
///        cross-`.so` symbol reference. Deliberately resolves NO QML-registered
///        object — an engine-init object lookup re-enters `initializeEngine`
///        (see `sharedCardPhotoStore()`).
LIBREKDE_PLASMOID_EXPORT void installCardPhotoProvider(QQmlEngine* engine);

} // namespace LibreKDE::Plasmoid
