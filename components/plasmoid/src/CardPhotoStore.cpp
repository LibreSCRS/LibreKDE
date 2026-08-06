// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "CardPhotoStore.h"

#include <QGlobalStatic>

#include <memory>

namespace LibreKDE::Plasmoid {

// The one process-wide CardPhotoStore, shared by SmartCardHandler (the GUI-thread
// writer) and the QML CardPhotoProvider (the image-provider reader). Q_GLOBAL_STATIC
// is the sanctioned shared-state idiom for Qt hosts in this project;
// it decouples the store from the `SmartCard` QML singleton so
// installCardPhotoProvider() need not resolve that singleton at engine init —
// resolving it there re-enters the module's own initializeEngine
// (singletonInstance -> initializeEngine -> installCardPhotoProvider -> ...) and
// overflows the stack.
Q_GLOBAL_STATIC_WITH_ARGS(std::shared_ptr<CardPhotoStore>, g_sharedPhotoStore, (std::make_shared<CardPhotoStore>()))

std::shared_ptr<CardPhotoStore> sharedCardPhotoStore()
{
    return *g_sharedPhotoStore;
}

} // namespace LibreKDE::Plasmoid
