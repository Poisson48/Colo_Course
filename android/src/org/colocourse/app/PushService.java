package org.colocourse.app;

import android.app.Notification;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import android.os.IBinder;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

// Veille ntfy en arrière-plan : réveille l'app quand une autre personne modifie une liste.
// Foreground service obligatoire sur Android 8+ (notification discrète « veille »).
public class PushService extends Service {

    private static final String PREFS = "colocourse_push";
    private static final int FG_ID = 4546;

    private volatile Thread worker;
    private volatile boolean running;

    public static void configure(Context ctx, String baseUrl, String[] topics) {
        if (ctx == null)
            return;
        SharedPreferences prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        prefs.edit()
                .putString("baseUrl", baseUrl != null ? baseUrl.trim() : "")
                .putStringSet("topics", new HashSet<>(Arrays.asList(topics != null ? topics : new String[0])))
                .apply();

        Intent intent = new Intent(ctx, PushService.class);
        if (baseUrl == null || baseUrl.isEmpty() || topics == null || topics.length == 0) {
            ctx.stopService(intent);
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            ctx.startForegroundService(intent);
        else
            ctx.startService(intent);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        SharedPreferences prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        final String baseUrl = prefs.getString("baseUrl", "");
        final Set<String> topics = prefs.getStringSet("topics", null);

        if (baseUrl.isEmpty() || topics == null || topics.isEmpty()) {
            stopSelf();
            return START_NOT_STICKY;
        }

        Platform.createChannel(this);
        Notification.Builder builder = new Notification.Builder(this, Platform.CHANNEL_ID)
                .setSmallIcon(smallIcon())
                .setContentTitle("Colo Course")
                .setContentText("Veille des listes partagées")
                .setOngoing(true);
        startForeground(FG_ID, builder.build());

        if (worker != null && worker.isAlive()) {
            running = true;
            return START_STICKY;
        }

        running = true;
        worker = new Thread(() -> pollLoop(baseUrl, topics), "ColoPush");
        worker.start();
        return START_STICKY;
    }

    private void pollLoop(String baseUrl, Set<String> topics) {
        String root = baseUrl.endsWith("/") ? baseUrl : baseUrl + "/";
        SharedPreferences prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE);

        while (running) {
            for (String topic : topics) {
                if (!running)
                    break;
                try {
                    final String sinceKey = "since_" + topic;
                    final String since = prefs.getString(sinceKey, "");
                    String pollUrl = root + topic + "/json?poll=1&since=" + since;
                    HttpURLConnection conn = (HttpURLConnection) new URL(pollUrl).openConnection();
                    conn.setRequestMethod("GET");
                    conn.setConnectTimeout(15000);
                    conn.setReadTimeout(90000);

                    if (conn.getResponseCode() != 200) {
                        conn.disconnect();
                        sleep(5000);
                        continue;
                    }

                    StringBuilder sb = new StringBuilder();
                    try (BufferedReader br = new BufferedReader(
                            new InputStreamReader(conn.getInputStream(), StandardCharsets.UTF_8))) {
                        String line;
                        while ((line = br.readLine()) != null)
                            sb.append(line);
                    }
                    conn.disconnect();

                    JSONArray arr = new JSONArray(sb.toString());
                    for (int i = 0; i < arr.length(); ++i) {
                        JSONObject msg = arr.getJSONObject(i);
                        final String id = msg.optString("id", "");
                        if (!id.isEmpty())
                            prefs.edit().putString(sinceKey, id).apply();

                        final String event = msg.optString("event", "message");
                        if (!"message".equals(event))
                            continue;

                        String title = msg.optString("title", "");
                        if (title.isEmpty())
                            title = "Liste mise à jour";
                        String body = msg.optString("message", "Synchronisation…");
                        Platform.showNotification(PushService.this, title, body);
                    }
                } catch (Exception e) {
                    sleep(8000);
                }
            }
            sleep(2000);
        }
    }

    private static void sleep(long ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private int smallIcon() {
        int id = getResources().getIdentifier("ic_stat_notify", "drawable", getPackageName());
        return id != 0 ? id : android.R.drawable.stat_notify_sync;
    }

    @Override
    public void onDestroy() {
        running = false;
        if (worker != null)
            worker.interrupt();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
