// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import org.kde.kirigami as Kirigami

// Reusable fade-IN for Loader item swaps. This
// is deliberately a fade-in, NOT a crossfade: a Loader destroys the outgoing
// item instantly, so only the incoming item can animate. Call reveal(item)
// from the Loader's onLoaded.
NumberAnimation {
    property: "opacity"
    from: 0.0
    to: 1.0
    duration: Kirigami.Units.longDuration
    easing.type: Easing.OutCubic

    function reveal(item) {
        if (!item) {
            return
        }
        item.opacity = 0.0
        target = item
        restart()
    }
}
