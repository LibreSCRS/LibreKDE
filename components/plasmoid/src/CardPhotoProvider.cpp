// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "CardPhotoProvider.h"

#include "CardPhotoStore.h"

#include <QQmlEngine>

namespace LibreKDE::Plasmoid {

CardPhotoProvider::CardPhotoProvider(std::shared_ptr<CardPhotoStore> store)
    : QQuickImageProvider(QQuickImageProvider::Image), m_store(std::move(store))
{}

quint64 CardPhotoProvider::slotFromImageId(const QString& id)
{
    // "cardphoto/<slot>[?<token>]" -> <slot>; 0 (never allocated — the handler
    // counter starts at 1) on any malformed id, which resolves to a null image.
    const qsizetype slash = id.indexOf(QLatin1Char('/'));
    if (slash < 0) {
        return 0;
    }
    qsizetype end = id.indexOf(QLatin1Char('?'), slash + 1);
    if (end < 0) {
        end = id.size();
    }
    bool ok = false;
    const quint64 slot = id.mid(slash + 1, end - slash - 1).toULongLong(&ok);
    return ok ? slot : 0;
}

QImage CardPhotoProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    // The URL is image://librekde/cardphoto/<slot>?<token>: the `id` therefore
    // reads "cardphoto/<slot>?<token>". The slot addresses the requesting
    // widget's entry in the shared store (per-widget isolation — several
    // plasmoid instances show different cards concurrently); the token is only
    // an opaque cache-buster. An empty/unknown slot yields a null QImage, which
    // a QML Image renders as nothing.
    QImage image = m_store ? m_store->image(slotFromImageId(id)) : QImage();
    if (size != nullptr) {
        *size = image.size();
    }
    if (!image.isNull() && requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0) {
        return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

void installCardPhotoProvider(QQmlEngine* engine)
{
    if (engine == nullptr) {
        return;
    }
    // Bind the provider to the process-SHARED store: every per-widget
    // SmartCardHandler writes its decoded photo into this same global store
    // (under its own slot), so the provider stays valid regardless of when — or
    // whether — any handler is constructed, and NO QML-registered object is
    // resolved at engine init (an engine-init object lookup re-enters this
    // module's initializeEngine and overflows the stack — the historical
    // singletonInstance bug). addImageProvider takes ownership of the provider.
    engine->addImageProvider(QStringLiteral("librekde"), new CardPhotoProvider(sharedCardPhotoStore()));
}

} // namespace LibreKDE::Plasmoid
