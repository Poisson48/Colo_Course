// Quick live relay smoke test (not run in CI by default).
// Build: cmake --build build --target tst_relay_live && ./build/tests/tst_relay_live

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTimer>

#include "net/relayclient.h"
#include "net/nostr.h"
#include "net/crypto.h"

class TstRelayLive : public QObject
{
    Q_OBJECT

private slots:
    void coloApps_connectAndSubscribe()
    {
        net::RelayClient client(QUrl(QStringLiteral("wss://colo-apps.les-crevettes-cevenoles.fr")));
        QSignalSpy connected(&client, &net::RelayClient::connected);
        QSignalSpy eose(&client, &net::RelayClient::eose);

        client.connectToRelay();
        QVERIFY2(connected.wait(15000), "relay connect timeout");

        client.subscribe(QStringLiteral("colo-smoke-test"), 0);
        QVERIFY2(eose.wait(10000), "EOSE timeout");

        client.disconnectFromRelay();
    }

    void coloApps_publishSignedKind4545()
    {
        net::RelayClient client(QUrl(QStringLiteral("wss://colo-apps.les-crevettes-cevenoles.fr")));
        QSignalSpy connected(&client, &net::RelayClient::connected);
        QSignalSpy ack(&client, &net::RelayClient::publishAck);

        client.connectToRelay();
        QVERIFY2(connected.wait(15000), "relay connect timeout");

        std::vector<uint8_t> seed(32, 0x42);
        net::NostrEvent ev;
        ev.created_at = QDateTime::currentSecsSinceEpoch();
        ev.kind = 4545;
        ev.content = QStringLiteral("smoke");
        QJsonArray tag;
        tag.append(QStringLiteral("t"));
        tag.append(QStringLiteral("colo-smoke-test"));
        ev.tags.append(tag);

        QVERIFY(net::signEvent(ev, seed));
        client.publish(ev);

        QVERIFY2(ack.wait(10000), "publish ack timeout");
        QCOMPARE(ack.at(0).at(0).toString(), ev.id);
        QVERIFY2(ack.at(0).at(1).toBool(), qPrintable(
            QStringLiteral("publish rejected: %1").arg(ack.at(0).at(2).toString())));

        client.disconnectFromRelay();
    }
};

QTEST_MAIN(TstRelayLive)
#include "tst_relay_live.moc"
