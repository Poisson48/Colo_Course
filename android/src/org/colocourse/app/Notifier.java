package org.colocourse.app;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;

// Notification locale après un merge distant (SPEC §8). Appelée depuis C++ via JNI
// (src/app/notifier.cpp). Aucune référence à la classe R générée : les ressources
// sont résolues par nom, pour ne dépendre d'aucun namespace Gradle.
public class Notifier {

    private static final String CHANNEL_ID = "colocourse.sync";
    private static final int    NOTIFICATION_ID = 4545;
    private static final int    PERMISSION_REQUEST = 4545;

    public static void createChannel(Context ctx) {
        if (ctx == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.O)
            return;
        NotificationManager nm = ctx.getSystemService(NotificationManager.class);
        if (nm == null || nm.getNotificationChannel(CHANNEL_ID) != null)
            return;
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, "Synchronisation", NotificationManager.IMPORTANCE_DEFAULT);
        channel.setDescription("Changements reçus sur vos listes");
        nm.createNotificationChannel(channel);
    }

    // Android 13+ : POST_NOTIFICATIONS est une permission runtime. Sans activité
    // (cas d'un service), on ne peut pas la demander : on sort silencieusement.
    public static void requestPermission(Context ctx) {
        if (!(ctx instanceof Activity) || Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU)
            return;
        Activity activity = (Activity) ctx;
        if (activity.checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
            activity.requestPermissions(
                    new String[]{ android.Manifest.permission.POST_NOTIFICATIONS },
                    PERMISSION_REQUEST);
        }
    }

    public static void showNotification(Context ctx, String title, String body) {
        if (ctx == null)
            return;
        createChannel(ctx);
        NotificationManager nm = ctx.getSystemService(NotificationManager.class);
        if (nm == null)
            return;

        Notification.Builder builder = new Notification.Builder(ctx, CHANNEL_ID)
                .setSmallIcon(smallIcon(ctx))
                .setContentTitle(title)
                .setContentText(body)
                .setStyle(new Notification.BigTextStyle().bigText(body))
                .setAutoCancel(true);

        // Tap sur la notif → ouvre l'app.
        Intent open = ctx.getPackageManager().getLaunchIntentForPackage(ctx.getPackageName());
        if (open != null) {
            open.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            builder.setContentIntent(PendingIntent.getActivity(
                    ctx, 0, open,
                    PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE));
        }

        nm.notify(NOTIFICATION_ID, builder.build());
    }

    private static int smallIcon(Context ctx) {
        int id = ctx.getResources().getIdentifier(
                "ic_stat_notify", "drawable", ctx.getPackageName());
        return id != 0 ? id : android.R.drawable.stat_notify_sync;
    }
}
