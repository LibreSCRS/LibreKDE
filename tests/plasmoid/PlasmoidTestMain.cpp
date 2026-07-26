// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// gtest entry point for the plasmoid D-Bus tests. A QGuiApplication (under the
// offscreen QPA) is required because SmartCardHandler::copyField uses QClipboard,
// which only exists on a QGuiApplication. QtDBus dispatch + QImage decode work
// unchanged under it.

#include <QGuiApplication>
#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
