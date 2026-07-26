// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// gtest entry point for the D-Bus FakeAgent tests: a QCoreApplication must
// exist for the Qt event loop / QtDBus dispatch the harness drives.

#include <QCoreApplication>
#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
