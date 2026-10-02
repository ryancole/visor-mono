pragma Singleton
import QtQuick

QtObject {
    readonly property color background: "#e6141418"
    readonly property color popupBackground: "#f7141418" // launcher, menus: less see-through
    readonly property color surface: "#26ffffff"
    readonly property color text: "#e8e8ee"
    readonly property color subtext: "#9a9aa8"
    readonly property color accent: "#8ab4ff"

    readonly property string font: "Segoe UI Variable Text"
    readonly property string iconFont: "Segoe Fluent Icons"
    readonly property int fontSize: 13
    readonly property int barHeight: 32
}
