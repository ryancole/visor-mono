import QtQuick

// A glyph from Segoe Fluent Icons, e.g. Icon { glyph: "" }.
Text {
    property string glyph
    text: glyph
    color: Theme.text
    font.family: Theme.iconFont
    font.pixelSize: Theme.fontSize + 1
    verticalAlignment: Text.AlignVCenter
    renderType: Text.NativeRendering
}
