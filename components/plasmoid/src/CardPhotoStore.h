// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>

#include <memory>

namespace LibreKDE::Plasmoid {

/// @brief Thread-safe, slot-keyed holder for card ID photos, shared (by
///        `std::shared_ptr`) between the GUI-thread writers (each per-widget
///        `SmartCardHandler`, which decodes the agent's sealed-memfd photo into
///        a `QImage` under its own process-unique slot) and the
///        `QQuickImageProvider` reader (`CardPhotoProvider`).
///
/// Keyed by slot because Plasma 6 hosts every plasmoid instance in ONE shared
/// engine: several handlers (one per widget) write concurrently-live photos of
/// DIFFERENT cards into this one process-wide store; the slot (baked into each
/// handler's `image://librekde/cardphoto/<slot>?<token>` URL) keeps them
/// isolated per widget.
///
/// QQuickImageProvider::requestImage may be invoked from a non-GUI thread, while
/// the handlers mutate their slots on the GUI thread; a mutex makes the
/// read/write pairs race-free. The store is held by shared_ptr so the provider
/// can outlive any handler/engine teardown order without dereferencing freed
/// memory.
///
/// The store is NOT a QObject and holds NO secrets beyond the (already
/// public-by-design) face photos; it never logs the image bytes.
class CardPhotoStore
{
public:
    CardPhotoStore() = default;

    /// @brief Replace @p slot's image with @p image. GUI-thread writer.
    void setImage(quint64 slot, const QImage& image)
    {
        QMutexLocker locker(&m_mutex);
        m_images.insert(slot, image);
    }

    /// @brief Scrub @p slot's image (card removal / new read / handler teardown).
    void clear(quint64 slot)
    {
        QMutexLocker locker(&m_mutex);
        m_images.remove(slot);
    }

    /// @brief A copy of @p slot's image (implicitly shared — cheap); null when
    ///        the slot is empty. Reentrant; callable from the QML image-provider
    ///        thread.
    [[nodiscard]] QImage image(quint64 slot) const
    {
        QMutexLocker locker(&m_mutex);
        return m_images.value(slot);
    }

    /// @brief True iff @p slot currently holds a non-null image.
    [[nodiscard]] bool hasImage(quint64 slot) const
    {
        QMutexLocker locker(&m_mutex);
        return !m_images.value(slot).isNull();
    }

private:
    mutable QMutex m_mutex;
    QHash<quint64, QImage> m_images;
};

/// @brief The process-wide shared CardPhotoStore. Every `SmartCardHandler`
///        (one GUI-thread writer per widget, each under its own slot) and
///        `CardPhotoProvider` (the image-provider reader) bind to this ONE
///        store, so `installCardPhotoProvider()` (invoked inside the module's
///        own `initializeEngine`) never has to resolve ANY QML-registered
///        object at engine init — resolving one there re-enters
///        `initializeEngine` (historically via `QQmlEngine::singletonInstance`
///        → unbounded recursion → stack overflow; the same hazard applies to
///        any engine-time object lookup). Backed by `Q_GLOBAL_STATIC` — the
///        LibreKDE shared-Qt-state pattern, as used by `shared/session`'s
///        `CardSessionContext`.
[[nodiscard]] std::shared_ptr<CardPhotoStore> sharedCardPhotoStore();

} // namespace LibreKDE::Plasmoid
