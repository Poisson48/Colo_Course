#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include <QQmlEngine>

#include "appcontroller.h"
#include "permissions.h"
#include "platform.h"
#include "qrimageprovider.h"
#include "qrscanner.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ColoCourse");
    app.setApplicationName("ColoCourse");

    // L'UI s'appuie sur les attachements Material (theme, elevation, accent) :
    // ils sont ignorés si un autre style est actif.
    QQuickStyle::setStyle(QStringLiteral("Material"));

    // Canal de notification + permission POST_NOTIFICATIONS (Android 13+).
    app::initNotifications();

    app::AppController controller;
    if (!controller.init()) {
        qCritical("AppController::init() failed");
        return 1;
    }

    // Lien d'invitation ouvert depuis une autre app (WhatsApp, SMS…) : Android
    // délivre l'intent VIEW ici, y compris quand l'app tournait déjà.
    QDesktopServices::setUrlHandler(QStringLiteral("colocourse"),
                                    &controller, "handleJoinUrl");

    // SPEC §8 : au retour au premier plan, rattrapage immédiat — Android a pu tuer
    // la socket pendant la mise en veille, et la souscription doit être rejouée.
    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                     &controller, [&controller](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive)
            controller.syncEngine()->subscribeAllLists();
    });

    // Types du scanner, dans le même module QML que les écrans (URI ColoCourse).
    app::Permissions permissions;
    qmlRegisterSingletonInstance("ColoCourse", 1, 0, "Permissions", &permissions);
    qmlRegisterType<app::QrScanner>("ColoCourse", 1, 0, "QrScanner");

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("qr"), new app::QrImageProvider());
    engine.rootContext()->setContextProperty(QStringLiteral("AppController"), &controller);

    const QUrl url(QStringLiteral("qrc:/ColoCourse/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
