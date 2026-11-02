// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// The widget's own controls, clicked in the widget's own QML. The card view is a
// Loader that swaps its state component whenever `smartCard.state` changes, and
// the state component holds the button that was clicked. "Start service" and
// "Refresh" wait for the agent in local event loops; whatever those loops
// deliver -- the agent coming up, a card going in -- swaps the component, and a
// deferred delete posted from one local loop runs in the next one on the same
// call stack. Run under the click, that deletes the button while its release
// handler is still on the stack, which then writes into the freed button.
//
// Each case clicks the real button in the real CardDetail.qml, loaded through
// the real `org.librescrs.smartcard` plugin of this build, and records whether
// the button was destroyed between its `released()` and the end of its
// `clicked()` -- the window in which the release handler is still running. The
// view must still be replaced afterwards: a click that changed nothing would
// pass without proving anything.
//
// The bus has the agent installed but not running (librekde_activatable_agent_bus):
// it claims its name a second after the bus starts it.

#include "TestBus.h" // waitFor

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFile>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>

#include <gtest/gtest.h>

#include <functional>
#include <memory>

using LibreSCRS::AgentClient::Fakes::waitFor;

namespace {

// CardStateModel::State, as `smartCard.state` carries it.
constexpr int kNoCard = 0;
constexpr int kAgentUnavailable = 7;

const QString kAgentName = QStringLiteral("org.librescrs.Agent");

bool agentOnBus()
{
    return QDBusConnection::sessionBus().interface()->isServiceRegistered(kAgentName).value();
}

// Watches one button through one click. The release handler emits released()
// and then clicked(); QML signal handlers run before any C++ connection, so the
// slot on clicked() runs after the button's own onClicked has returned. A
// button destroyed between the two was destroyed under its own release handler.
class ClickProbe : public QObject
{
    Q_OBJECT
public:
    explicit ClickProbe(QObject* button)
    {
        connect(button, SIGNAL(released()), this, SLOT(onReleased()));
        connect(button, SIGNAL(clicked()), this, SLOT(onClicked()));
        connect(button, &QObject::destroyed, this, [this] {
            if (inClick) {
                destroyedUnderClick = true;
            }
        });
    }

    std::function<void()> duringClick; ///< Run from released(), before onClicked.
    bool released = false;
    bool inClick = false;
    bool destroyedUnderClick = false;

private Q_SLOTS:
    void onReleased()
    {
        released = true;
        inClick = true;
        if (duringClick) {
            duringClick();
        }
    }
    void onClicked()
    {
        inClick = false;
    }
};

class AgentControlClick : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_FALSE(agentOnBus()) << "the agent must not be running before the test asks for it";
        engine = std::make_unique<QQmlEngine>();
        engine->addImportPath(QStringLiteral(LIBREKDE_QML_IMPORT_DIR));

        QQmlComponent card(engine.get());
        card.setData("import org.librescrs.smartcard\nSmartCard {}\n", QUrl());
        smartCard.reset(card.create());
        ASSERT_NE(smartCard, nullptr) << card.errorString().toStdString();
        ASSERT_TRUE(pluginFromThisBuild())
            << "org.librescrs.smartcard was loaded from somewhere other than " LIBREKDE_QML_IMPORT_DIR;

        QQmlComponent detail(engine.get(),
                             QUrl::fromLocalFile(QStringLiteral(LIBREKDE_PLASMOID_UI_DIR "/CardDetail.qml")));
        view.reset(qobject_cast<QQuickItem*>(
            detail.createWithInitialProperties({{QStringLiteral("smartCard"), QVariant::fromValue(smartCard.get())}})));
        ASSERT_NE(view, nullptr) << detail.errorString().toStdString();

        window = std::make_unique<QQuickWindow>();
        window->resize(480, 640);
        view->setParentItem(window->contentItem());
        view->setSize(QSizeF(window->width(), window->height()));
        window->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window.get()));
    }

    void TearDown() override
    {
        view.reset();
        window.reset();
        smartCard.reset();
        engine.reset();
    }

    int state() const
    {
        return smartCard->property("state").toInt();
    }

    QQuickItem* loader() const
    {
        for (auto* item : view->findChildren<QQuickItem*>()) {
            if (item->inherits("QQuickLoader")) {
                return item;
            }
        }
        return nullptr;
    }

    // The visible, enabled button with icon @p iconName in the loaded state
    // component, once it has been laid out.
    QQuickItem* button(const QString& iconName) const
    {
        QQuickItem* l = loader();
        auto* loaded = l ? l->property("item").value<QQuickItem*>() : nullptr;
        if (!loaded) {
            return nullptr;
        }
        for (auto* item : loaded->findChildren<QQuickItem*>()) {
            if (QQmlProperty(item, QStringLiteral("icon.name")).read().toString() == iconName && item->isVisible() &&
                item->isEnabled() && item->width() > 0 && item->height() > 0) {
                return item;
            }
        }
        return nullptr;
    }

    void click(QQuickItem* target)
    {
        const QPointF centre = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
        QTest::mouseClick(window.get(), Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    }

    static bool pluginFromThisBuild()
    {
        QFile maps(QStringLiteral("/proc/self/maps"));
        if (!maps.open(QIODevice::ReadOnly)) {
            return false;
        }
        return maps.readAll().contains(LIBREKDE_QML_IMPORT_DIR "/org/librescrs/smartcard/");
    }

    std::unique_ptr<QQmlEngine> engine;
    std::unique_ptr<QObject> smartCard;
    std::unique_ptr<QQuickItem> view;
    std::unique_ptr<QQuickWindow> window;
};

} // namespace

TEST_F(AgentControlClick, StartServiceIsNotDestroyedUnderItsOwnClick)
{
    ASSERT_EQ(state(), kAgentUnavailable);
    QQuickItem* start = nullptr;
    ASSERT_TRUE(waitFor([&] { return (start = button(QStringLiteral("system-run"))) != nullptr; }))
        << "no enabled \"Start service\" button in the agent-unavailable view";
    QPointer<QQuickItem> watched(start);
    ClickProbe probe(start);

    click(start);

    ASSERT_TRUE(probe.released) << "the click never reached the button";
    EXPECT_FALSE(probe.destroyedUnderClick)
        << "the view holding \"Start service\" was replaced while the button's click was still running";
    EXPECT_TRUE(waitFor([] { return agentOnBus(); }));
    EXPECT_TRUE(waitFor([&] { return state() != kAgentUnavailable; }));
    EXPECT_TRUE(waitFor([&] { return watched.isNull(); }))
        << "the agent's arrival never replaced the view, so the click proved nothing";
}

TEST_F(AgentControlClick, RefreshThatStartsTheAgentIsNotDestroyedUnderItsOwnClick)
{
    ASSERT_EQ(state(), kAgentUnavailable);
    QQuickItem* refresh = nullptr;
    ASSERT_TRUE(waitFor([&] { return (refresh = button(QStringLiteral("view-refresh"))) != nullptr; }))
        << "no enabled \"Refresh\" button in the agent-unavailable view";
    QPointer<QQuickItem> watched(refresh);
    ClickProbe probe(refresh);

    click(refresh);

    ASSERT_TRUE(probe.released) << "the click never reached the button";
    EXPECT_FALSE(probe.destroyedUnderClick)
        << "the view holding \"Refresh\" was replaced while the button's click was still running";
    EXPECT_TRUE(waitFor([] { return agentOnBus(); }));
    EXPECT_TRUE(waitFor([&] { return state() != kAgentUnavailable; }));
    EXPECT_TRUE(waitFor([&] { return watched.isNull(); }))
        << "the agent's arrival never replaced the view, so the click proved nothing";
}

// With the agent already running, a refresh still waits twice: once for the
// bus to confirm the agent, once for the agent's object list. A card event
// delivered during the first wait swaps the view; that swap is played here by
// the Loader itself, from a zero-delay timer the click starts, which runs in
// whichever event loop turns first after the click.
TEST_F(AgentControlClick, RefreshOfARunningAgentIsNotDestroyedUnderItsOwnClick)
{
    QDBusConnection::sessionBus().interface()->startService(kAgentName);
    ASSERT_TRUE(waitFor([&] { return agentOnBus() && state() != kAgentUnavailable; }, 8000));
    // A reader that is not on the bus leaves the widget card-less, which is a
    // view with a Refresh button whatever the double's own reader holds.
    ASSERT_TRUE(smartCard->setProperty("boundReaderName", QStringLiteral("Absent reader")));
    QQuickItem* refresh = nullptr;
    ASSERT_TRUE(waitFor([&] {
        return state() == kNoCard && (refresh = button(QStringLiteral("view-refresh"))) != nullptr;
    })) << "no enabled \"Refresh\" button in the card-less view";
    QPointer<QQuickItem> watched(refresh);
    QPointer<QQuickItem> stateLoader(loader());
    ASSERT_FALSE(stateLoader.isNull());
    ClickProbe probe(refresh);
    probe.duringClick = [stateLoader] {
        QTimer::singleShot(0, stateLoader.data(), [stateLoader] {
            stateLoader->setProperty("sourceComponent", QVariant::fromValue<QQmlComponent*>(nullptr));
        });
    };

    click(refresh);

    ASSERT_TRUE(probe.released) << "the click never reached the button";
    EXPECT_FALSE(probe.destroyedUnderClick)
        << "the view holding \"Refresh\" was replaced while the button's click was still running";
    EXPECT_TRUE(waitFor([&] { return watched.isNull(); }))
        << "the view was never replaced, so the click proved nothing";
}

#include "AgentControlClickTest.moc"
