#pragma once

#include <QString>

namespace app {

// Pont vers les services natifs (Android : org.colocourse.app.Platform via JNI).
// Chaque fonction est un no-op renvoyant false hors Android, l'appelant fournit
// alors un repli desktop.

// Prépare le canal de notification et demande POST_NOTIFICATIONS (Android 13+).
// À appeler une fois au démarrage.
void initNotifications();

// Notification système native. false → l'appelant retombe sur QSystemTrayIcon.
bool platformNotify(const QString& title, const QString& body);

// Feuille de partage native (ACTION_SEND). false → l'appelant copie le texte.
bool platformShare(const QString& text);

} // namespace app
