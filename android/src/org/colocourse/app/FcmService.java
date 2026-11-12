package org.colocourse.app;

import com.google.firebase.messaging.FirebaseMessagingService;
import com.google.firebase.messaging.RemoteMessage;

// Message FCM data-only : aucun contenu de liste. On relance la synchro courte.
public class FcmService extends FirebaseMessagingService {
    @Override
    public void onMessageReceived(RemoteMessage message) {
        Platform.startBackgroundSyncService(this);
        Platform.showNotification(this, "Liste mise à jour",
                "Ouvrez l'app pour voir le détail.");
    }
}
