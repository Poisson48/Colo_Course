// tst_updater.cpp — parseManifest, isNewer, notesFromBody (sans réseau).

#include "app/updater.h"

#include <QString>

#include <cstdio>

static int g_total = 0, g_passed = 0, g_failed = 0;

#define EXPECT_TRUE(expr)                                                      \
    do { ++g_total;                                                           \
        if (!(expr)) { ++g_failed;                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } \
        else ++g_passed; } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using app::Updater;

static void test_parseManifest() {
    const QByteArray json =
        "{\"version\":\"0.28.0\","
        "\"publishedAt\":\"2026-08-30T12:00:00Z\","
        "\"notes\":\"Filtre catégories corrigé.\","
        "\"apkUrl\":\"https://colo-apps.example/releases/app.apk\","
        "\"changelog\":["
        "{\"version\":\"0.28.0\",\"notes\":\"Filtre\",\"publishedAt\":\"2026-08-30T12:00:00Z\"},"
        "{\"version\":\"0.27.0\",\"notes\":\"Ancienne\",\"publishedAt\":\"2026-08-01T12:00:00Z\"}"
        "]}";

    Updater::ManifestData data;
    EXPECT_TRUE(Updater::parseManifest(json, &data));
    EXPECT_EQ(data.version, QStringLiteral("0.28.0"));
    EXPECT_EQ(data.apkUrl, QStringLiteral("https://colo-apps.example/releases/app.apk"));
    EXPECT_EQ(data.changelog.size(), 2);

    EXPECT_TRUE(!Updater::parseManifest(QByteArray("{"), &data));
    EXPECT_TRUE(!Updater::parseManifest(QByteArray("{\"version\":\"\"}"), &data));
}

static void test_isNewer() {
    EXPECT_TRUE(Updater::isNewer("0.28.0", "0.27.14"));
    EXPECT_TRUE(!Updater::isNewer("0.27.14", "0.28.0"));
}

static void test_notesFromBody() {
    const QString body = "Nouveautés\n---\nInstallation";
    EXPECT_EQ(Updater::notesFromBody(body), QStringLiteral("Nouveautés"));
}

int main() {
    std::printf("=== tst_updater ===\n");
    test_parseManifest();
    test_isNewer();
    test_notesFromBody();
    std::printf("\nResults: %d/%d passed, %d failed\n", g_passed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
