// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.librescrs.smartcard

// Per-instance reader binding. Populates the chooser from the
// agent's LIVE reader list (smartCard.availableReaderNames) but stays EDITABLE
// so a widget can be pinned to a reader that is not currently present
// (remembered by friendly name). The Auto sentinel maps to the empty string.
Kirigami.FormLayout {
    id: page

    // The config dialog runs in its own QQmlEngine, so it instantiates its own
    // handler to read the live reader roster. Cheap: the handler co-owns the
    // process-shared AgentClient (sharedAgentClient), so no second agent
    // connection or ObjectManager discovery is made.
    readonly property SmartCard smartCard: SmartCard { }

    // Plasma binds cfg_<key> to Plasmoid.configuration.<key>. A plain string
    // (not a control alias) because the UI maps the Auto sentinel <-> "".
    property string cfg_boundReaderName: ""
    property string cfg_boundReaderNameDefault: ""

    readonly property string autoLabel:
        i18nc("@item:inlistbox reader binding follows the active card",
              "Auto (follow active card)")

    // Reader chooser + a re-scan affordance on the same row. The combo is
    // populated from the agent's LIVE roster, but a reader plugged in while this
    // dialog is open (or one the client's discovery missed) may not be listed
    // yet — the refresh button forces a fresh GetManagedObjects so a newly-added
    // reader can be selected here without reopening the dialog.
    RowLayout {
        Kirigami.FormData.label: i18nc("@label:listbox plasmoid reader binding", "Reader:")
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

    QQC2.ComboBox {
        id: readerCombo
        Layout.fillWidth: true
        // Cap the width DEMAND: an editable ComboBox's implicit width follows
        // its longest text, and the config dialog clips content wider than its
        // viewport instead of scrolling horizontally. fillWidth still stretches
        // it in wider dialogs.
        Layout.preferredWidth: Kirigami.Units.gridUnit * 18
        editable: true

        // The model carries a FRIENDLY display label (`text`) plus the RAW
        // reader name (`value`) the binding is actually keyed on. The field and
        // dropdown show the short, distinguishable label; cfg_boundReaderName
        // always stores the raw name (or "" for Auto), so a stored binding stays
        // valid regardless of how the name is displayed. The two helpers below
        // map between them; a name typed free-form (an absent reader) has no
        // roster entry and round-trips verbatim.
        textRole: "text"
        valueRole: "value"

        function rosterModel() {
            var items = [{ text: page.autoLabel, value: "" }];
            var names = page.smartCard.availableReaderNames;
            for (var i = 0; i < names.length; ++i) {
                items.push({ text: page.smartCard.readerDisplayName(names[i]), value: names[i] });
            }
            return items;
        }
        // Raw binding value -> the label to show. Unknown (absent bound reader):
        // show its stored raw name verbatim so the binding is never hidden.
        function displayForValue(value) {
            if (value === "")
                return page.autoLabel;
            var m = model;
            for (var i = 0; i < m.length; ++i)
                if (m[i].value === value)
                    return m[i].text;
            return value;
        }
        // Field text -> the raw binding value. A label matching a roster entry
        // maps to that reader's raw name; the Auto label / empty field is "";
        // anything else is treated verbatim (a remembered absent reader).
        function valueForText(text) {
            if (text === page.autoLabel || text.length === 0)
                return "";
            var m = model;
            for (var i = 0; i < m.length; ++i)
                if (m[i].text === text)
                    return m[i].value;
            return text;
        }

        // The model is refreshed IMPERATIVELY, never via a live binding: a model
        // swap resets the editable field's editText to model[0] and that reset
        // round-trips into cfg_boundReaderName through onEditTextChanged BEFORE
        // any onModelChanged handler could restore it (verified empirically on
        // Qt 6.11). refreshFromRoster() saves the pending binding + in-progress
        // text across the swap and restores both:
        //  - focused: the text being typed is kept verbatim (incl. a cleared
        //    field — the clear-to-retype gesture is never repainted to Auto);
        //  - unfocused: the field re-shows the persisted binding as its label.
        function refreshFromRoster() {
            const saved = page.cfg_boundReaderName;
            const savedText = editText;
            const wasFocused = activeFocus;
            model = rosterModel();
            if (wasFocused) {
                editText = savedText;
            } else {
                editText = displayForValue(saved);
            }
            // Heal the round-tripped reset even when the editText restore was
            // a no-op (restored text == the reset text fires no change signal).
            page.cfg_boundReaderName = saved;
        }

        Connections {
            target: page.smartCard
            function onAvailableReaderNamesChanged() {
                readerCombo.refreshFromRoster();
            }
        }

        // Map the field text back onto the config key (raw name / "" for Auto).
        // Fires on popup-select AND free-form typing, so the config commits even
        // without pressing Enter. (A model swap passes through here too —
        // refreshFromRoster() restores the value it saved.)
        onEditTextChanged: {
            page.cfg_boundReaderName = valueForText(editText);
        }

        // Seed the model + field when the page opens. The field seed only
        // reliably covers the DEFAULT ("" -> Auto) case: Plasma injects the
        // loaded cfg_boundReaderName into this Loader item AFTER the child's
        // Component.onCompleted has already run, so a bound widget's real value
        // is not yet visible here.
        Component.onCompleted: refreshFromRoster()

        // Re-seed whenever Plasma writes the persisted binding into
        // cfg_boundReaderName (which, for a bound widget, happens AFTER onCompleted
        // above). Without this the field would keep showing "Auto" while the stored
        // binding is a real reader — the config UI would misrepresent it, and an
        // accidental edit could silently rebind it.
        //
        // Guarded on focus: while the USER is editing, their own edits round-trip
        // through cfg_boundReaderName, and an unguarded re-seed would instantly
        // rewrite the cleared field, fighting the clear-to-retype gesture. Plasma's
        // late injection happens while the field is NOT focused, so the seed still
        // lands in that case.
        Connections {
            target: page
            function onCfg_boundReaderNameChanged() {
                if (!readerCombo.activeFocus) {
                    readerCombo.editText = readerCombo.displayForValue(page.cfg_boundReaderName);
                }
            }
        }

        // Dropdown items show the friendly label with the FULL raw reader name on
        // hover, so two similar readers are always tellable apart while picking.
        delegate: QQC2.ItemDelegate {
            id: readerItem
            required property var modelData
            required property int index
            width: readerCombo.width
            text: readerItem.modelData.text
            highlighted: readerCombo.highlightedIndex === readerItem.index

            QQC2.ToolTip.text: readerItem.modelData.value
            QQC2.ToolTip.visible: readerItem.hovered && readerItem.modelData.value.length > 0
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }

        Accessible.role: Accessible.ComboBox
        Accessible.name: i18nc("@label:listbox plasmoid reader binding", "Reader:")
    }

        // Re-scan: force a fresh discovery so a reader plugged in while this
        // dialog is open appears in the chooser. Reuses the shared client's
        // GetManagedObjects path (no new wire); the roster refresh flows back
        // through availableReaderNamesChanged -> refreshFromRoster().
        QQC2.ToolButton {
            icon.name: "view-refresh"
            display: QQC2.AbstractButton.IconOnly
            text: i18nc("@action:button re-check the reader for a card", "Refresh")
            onClicked: page.smartCard.requestRefresh()

            QQC2.ToolTip.text: i18nc("@info:tooltip re-scan for readers",
                                     "Re-scan for readers")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            Accessible.role: Accessible.Button
            Accessible.name: QQC2.ToolTip.text
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        // A wrapping Text still reports its full single-line width as implicit
        // width; bound the demand so the help sentence wraps instead of pushing
        // the page past the dialog viewport (same cap as the combo above).
        Layout.preferredWidth: Kirigami.Units.gridUnit * 18
        wrapMode: Text.WordWrap
        opacity: 0.7
        text: i18nc("@info plasmoid reader-binding help",
                    "Bind this widget to a specific reader by name, or choose Auto to follow whichever card is active. A name that is not currently present is remembered until the reader is plugged in.")
    }
}
