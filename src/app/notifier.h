#pragma once

#include <QString>

namespace app {

// Prépare le canal de notification et demande POST_NOTIFICATIONS (Android 13+).
// À appeler une fois au démarrage. No-op hors Android.
void initNotifications();

// Notification système native. Retourne false si la plateforme n'en fournit pas :
// l'appelant retombe alors sur QSystemTrayIcon (desktop).
bool platformNotify(const QString& title, const QString& body);

} // namespace app
