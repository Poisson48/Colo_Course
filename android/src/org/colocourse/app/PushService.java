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
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

// Veille ntfy uniquement en arrière-plan (C++ arrête le service au premier plan).
public class PushService extends Service {

    private static final String PREFS = "colocourse_push";
    private static final int FG_ID = 4546;
    private static final long NOTIFY_COOLDOWN_MS = 60_000L;

    private volatile Thread worker;
    private volatile boolean running;

    public static void configure(Context ctx, String baseUrl, String[] topics,
                                 String deviceId) {
        if (ctx == null)
            return;
        SharedPreferences prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        prefs.edit()
                .putString("baseUrl", baseUrl != null ? baseUrl.trim() : "")
                .putString("deviceId", deviceId != null ? deviceId.trim() : "")
                .putStringSet("topics", new HashSet<>(Arrays.asList(
                        topics != null ? topics : new String[0])))
                .apply();

        Intent intent = new Intent(ctx, PushService.class);
        if (baseUrl == null || baseUrl.isEmpty() || topics == null || topics.length == 0) {
            ctx.stopService(intent);
            return;
        }
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                ctx.startForegroundService(intent);
            else
                ctx.startService(intent);
        } catch (Exception e) {
            // Android 12+ : impossible de lancer un FGS si l'app n'est pas au premier plan.
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        SharedPreferences prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        final String baseUrl = prefs.getString("baseUrl", "");
        final Set<String> topics = prefs.getStringSet("topics", null);

        if (baseUrl.isEmpty() || topics == null || topics.isEmpty()) {
            stopForeground(true);
            stopSelf();
            return START_NOT_STICKY;
        }

        Platform.createChannel(this);
        final CharSequence label = getApplicationInfo().loadLabel(getPackageManager());
        final String appName = label != null ? label.toString() : "Colo Course";
        Notification.Builder builder = new Notification.Builder(this, Platform.CHANNEL_VEILLE_ID)
                .setSmallIcon(smallIcon())
                .setContentTitle(appName)
                .setContentText("")
                .setOngoing(true)
                .setSilent(true)
                .setShowWhen(false)
                .setVisibility(Notification.VISIBILITY_SECRET);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            builder.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
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
        final String ownDevice = prefs.getString("deviceId", "");
        final String ownTag = ownDevice.isEmpty() ? "" : ("device:" + ownDevice);

        while (running) {
            for (String topic : topics) {
                if (!running)
                    break;
                try {
                    final String sinceKey = "since_" + topic;
                    final String since = prefs.getString(sinceKey, "");
                    final boolean priming = since.isEmpty();

                    String pollUrl = root + topic + "/json?poll=1";
                    if (!since.isEmpty())
                        pollUrl += "&since=" + URLEncoder.encode(since, "UTF-8");

                    HttpURLConnection conn = (HttpURLConnection) new URL(pollUrl).openConnection();
                    conn.setRequestMethod("GET");
                    conn.setConnectTimeout(15000);
                    conn.setReadTimeout(90000);

                    if (conn.getResponseCode() != 200) {
                        conn.disconnect();
                        sleep(8000);
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

                        if (priming)
                            continue;

                        if (!"message".equals(msg.optString("event", "message")))
                            continue;

                        if (!ownTag.isEmpty() && hasTag(msg, ownTag))
                            continue;

                        final long now = System.currentTimeMillis();
                        final long last = prefs.getLong("lastNotify_" + topic, 0L);
                        if (now - last < NOTIFY_COOLDOWN_MS)
                            continue;
                        prefs.edit().putLong("lastNotify_" + topic, now).apply();

                        String title = msg.optString("title", "");
                        if (title.isEmpty())
                            title = "Liste mise à jour";
                        Platform.showNotification(PushService.this, title,
                                "Modifications reçues — ouvrez l'app pour voir le détail");
                    }
                } catch (Exception e) {
                    sleep(8000);
                }
            }
            sleep(5000);
        }
    }

    private static boolean hasTag(JSONObject msg, String tag) {
        JSONArray tags = msg.optJSONArray("tags");
        if (tags == null)
            return false;
        for (int i = 0; i < tags.length(); ++i) {
            if (tag.equals(tags.optString(i, "")))
                return true;
        }
        return false;
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
        stopForeground(true);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
