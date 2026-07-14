#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCoreApplication>

#include "appcontroller.h"
#include "itemmodel.h"
#include "notifier.h"
#include "qrimageprovider.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ColoCourse");
    app.setApplicationName("ColoCourse");

    // Canal de notification + permission POST_NOTIFICATIONS (Android 13+).
    app::initNotifications();

    app::AppController controller;
    if (!controller.init()) {
        qCritical("AppController::init() failed");
        return 1;
    }

    // SPEC §8 : au retour au premier plan, rattrapage immédiat — Android a pu tuer
    // la socket pendant la mise en veille, et la souscription doit être rejouée.
    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                     &controller, [&controller](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive)
            controller.syncEngine()->subscribeAllLists();
    });

    QQmlApplicationEngine engine;

    // Register QR image provider
    engine.addImageProvider(QStringLiteral("qr"), new app::QrImageProvider());

    // Expose AppController and ItemModel factory to QML.
    engine.rootContext()->setContextProperty(QStringLiteral("AppController"), &controller);

    // ItemModel: one instance, reloaded per list.
    app::ItemModel itemModel;
    engine.rootContext()->setContextProperty(QStringLiteral("ItemModel"), &itemModel);

    // Connect AppController::listOpened to load the ItemModel for the chosen list.
    QObject::connect(&controller, &app::AppController::listOpened,
                     [&](const QString &listId, const QString & /*title*/) {
        itemModel.load(controller.db(), listId.toStdString(), controller.deviceId().toStdString());
    });

    const QUrl url(QStringLiteral("qrc:/ColoCourse/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
