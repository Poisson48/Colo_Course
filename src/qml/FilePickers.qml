import QtQuick
import QtQuick.Dialogs

// Sélecteurs de fichiers natifs pour l'export/import. Isolés ici parce qu'ils importent
// QtQuick.Dialogs, absent de certains kits Qt de bureau : l'écran des listes se charge
// sans, et ce fichier n'est instancié qu'au moment où l'on exporte ou importe.
// Sur Android, ces dialogues sont l'explorateur du système (SAF) et renvoient une URI
// content:// que le C++ sait lire et écrire.
Item {
    id: pickers

    function openExportCsv(listId) {
        exportCsvDialog.listId = listId
        exportCsvDialog.selectedFile = "file:///" + AppController.suggestedFileName(listId)
        exportCsvDialog.open()
    }
    function openExportZip() {
        exportZipDialog.selectedFile = "file:///mes-listes.zip"
        exportZipDialog.open()
    }
    function openImport() {
        importDialog.open()
    }

    FileDialog {
        id: exportCsvDialog
        fileMode: FileDialog.SaveFile
        nameFilters: ["Fichier CSV (*.csv)"]
        defaultSuffix: "csv"
        property string listId: ""
        onAccepted: AppController.exportListCsv(selectedFile, listId)
    }

    FileDialog {
        id: exportZipDialog
        fileMode: FileDialog.SaveFile
        nameFilters: ["Archive ZIP (*.zip)"]
        defaultSuffix: "zip"
        onAccepted: AppController.exportAllZip(selectedFile)
    }

    // Un .csv (une liste) ou un .zip (plusieurs). L'import crée de nouvelles listes,
    // il ne touche jamais aux existantes.
    FileDialog {
        id: importDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["Listes (*.csv *.zip)", "Tous les fichiers (*)"]
        onAccepted: AppController.importFile(selectedFile)
    }
}
