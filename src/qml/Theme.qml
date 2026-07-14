pragma Singleton
import QtQuick

// Palette unique de l'app (thème sombre). Toute couleur en dur ailleurs est un bug :
// c'est ici qu'on ajuste le contraste, pas dans les écrans.
QtObject {
    // Fonds, du plus profond au plus élevé.
    readonly property color background: "#101311"   // fond de l'app
    readonly property color surface:    "#1A1F1C"   // cartes, barres
    readonly property color surfaceHigh:"#232A25"   // champs, états pressés
    readonly property color outline:    "#2E3630"   // séparateurs, bordures

    // Vert de l'icône, éclairci pour rester lisible sur fond sombre.
    readonly property color accent:     "#6FCF7A"
    readonly property color accentDim:  "#2E7D32"

    readonly property color text:       "#ECF0ED"   // texte principal
    readonly property color textDim:    "#93A099"   // texte secondaire
    readonly property color danger:     "#E5534B"
    readonly property color warning:    "#E9B44C"

    // Rythme vertical et cibles tactiles : 48 est le minimum tactile Android.
    readonly property int radius:       16
    readonly property int touchTarget:  48
    readonly property int gap:          12
    readonly property int pad:          16
}
