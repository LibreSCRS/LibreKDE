// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "EngineInit.h"

#include "CardPhotoProvider.h"

#include <QtCore/qtsymbolmacros.h>

// The type-registration entry point qt_add_qml_module emits for this module. The
// auto-generated plugin source (which we replace via NO_GENERATE_PLUGIN_SOURCE)
// keeps this symbol alive so the linker does not drop the type registrations; we
// must do the same here.
QT_DECLARE_EXTERN_SYMBOL_VOID(qml_register_types_org_librescrs_smartcard)

namespace LibreKDE::Plasmoid {

EngineInit::EngineInit(QObject* parent) : QQmlEngineExtensionPlugin(parent)
{
    QT_KEEP_SYMBOL(qml_register_types_org_librescrs_smartcard);
}

void EngineInit::initializeEngine(QQmlEngine* engine, const char* /*uri*/)
{
    // Register the card-photo image provider as `librekde` so a QML Image with
    // source `image://librekde/cardphoto/<slot>?<token>` (smartCard.cardPhotoUrl)
    // resolves to that widget's decoded face photo. The lookup + registration
    // lives in the backing library (installCardPhotoProvider) so no symbol is
    // referenced across the hidden-visibility plugin `.so` boundary.
    //
    // CAVEAT: the generated qmldir declares `optional plugin`, so any
    // engine that imports org.librescrs.smartcard AFTER the module's types are
    // already process-registered SKIPS initializeEngine — that engine gets no
    // image://librekde provider. Harmless today: the only second engine is the
    // config dialog, which renders no card photos. A future phase that shows
    // card imagery outside the applet engine must install the provider on that
    // engine through a lazily-guarded path (e.g. from the consuming QML type)
    // instead of relying on this hook.
    //
    // i18n is deliberately NOT set up here: plasmashell's shared global engine
    // is not ours to mutate, and libplasma's per-applet KLocalizedQmlContext
    // (domain plasma_applet_org.librescrs.smartcard, installed for the applet
    // AND its config dialog) both shadows any root-context pin inside the
    // applet and resolves the applet-package QML strings against our catalogs
    // shipped under that domain.
    installCardPhotoProvider(engine);
}

} // namespace LibreKDE::Plasmoid
