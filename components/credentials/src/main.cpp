// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// librescrs-credentials-kde: the standalone credential-management window. A
// single-instance (KDBusService::Unique) Kirigami app launched by the plasmoid
// with a `--reader <object-path>` argument; a second launch raises + re-targets
// the running one. Secret-free — the agent's secure prompter collects PIN/PUK/
// CAN; this process never sees a secret.

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>

#include <KAboutData>
#include <KDBusService>
#include <KLocalizedContext>
#include <KLocalizedString>

#include "CredentialController.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("librekde");

    KAboutData about(QStringLiteral("librescrs-credentials"), i18nc("@title", "Card Credentials"),
                     QStringLiteral(LIBREKDE_VERSION_STRING),
                     i18n("Manage the PINs and signing key on your smart card"), KAboutLicense::GPL_V3);
    about.setDesktopFileName(QStringLiteral("org.librescrs.credentials"));
    KAboutData::setApplicationData(about);

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    QCommandLineParser parser;
    QCommandLineOption readerOpt(QStringList{QStringLiteral("reader")}, i18n("Reader object path to bind"),
                                 QStringLiteral("path"));
    parser.addOption(readerOpt);
    about.setupCommandLine(&parser);
    parser.process(app);
    about.processCommandLine(&parser);

    // Single-instance: a second launch raises + re-targets this one.
    KDBusService service(KDBusService::Unique);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.loadFromModule("org.librescrs.credentials", "Main");
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    auto* controller = engine.rootObjects().constFirst()->findChild<LibreKDE::Credentials::CredentialController*>();
    if (controller != nullptr && parser.isSet(readerOpt)) {
        controller->bindReader(parser.value(readerOpt));
    }

    QObject::connect(&service, &KDBusService::activateRequested, &app,
                     [controller, &engine](const QStringList& args, const QString& /*workingDir*/) {
                         // A second launch of the Unique service delivers its argv here —
                         // re-target to the requested reader AND raise/activate the window.
                         if (controller != nullptr) {
                             for (int i = 0; i + 1 < args.size(); ++i) {
                                 if (args.at(i) == QLatin1String("--reader")) {
                                     controller->bindReader(args.at(i + 1));
                                 }
                             }
                         }
                         if (!engine.rootObjects().isEmpty()) {
                             if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())) {
                                 window->show();
                                 window->raise();
                                 window->requestActivate();
                             }
                         }
                     });

    return app.exec();
}
