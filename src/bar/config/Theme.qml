pragma Singleton
import QtQuick
import Visor

// The config's look. The colours follow Windows' dark/light mode and accent
// colour (`Themes`; switch themes from the Win+X menu, or with
// Super+Ctrl+Shift+Space) and change live; the fonts and sizes are set here.
// To fix a colour whatever Windows says, replace its binding.
QtObject {
    readonly property color background: Qt.alpha(Themes.background, 0.9)
    readonly property color popupBackground: Qt.alpha(Themes.background, 0.97) // launcher, menus: less see-through
    readonly property color surface: Themes.surface // hover and selection
    readonly property color hover: Qt.alpha(Themes.text, 0.12) // a lighter touch
    readonly property color text: Themes.text
    readonly property color subtext: Themes.subtext
    readonly property color accent: Themes.accent

    readonly property string font: "Segoe UI Variable Text"
    readonly property string iconFont: "Segoe Fluent Icons"
    readonly property int fontSize: 13
    readonly property int barHeight: 32
}
