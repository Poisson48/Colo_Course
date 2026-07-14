#include "platform.h"

#ifdef Q_OS_ANDROID
#  include <QCoreApplication>
#  include <QJniObject>
#endif

namespace app {

#ifdef Q_OS_ANDROID

namespace {

constexpr const char* kPlatformClass = "org/colocourse/app/Platform";

// context() renvoie l'Activity quand l'app tourne au premier plan. Son type de
// retour a changé entre versions de Qt (QJniObject → jobject) : l'init par
// accolades accepte les deux.
QJniObject androidContext()
{
    return QJniObject{ QNativeInterface::QAndroidApplication::context() };
}

} // namespace

void initNotifications()
{
    const QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return;

    QJniObject::callStaticMethod<void>(
        kPlatformClass, "createChannel",
        "(Landroid/content/Context;)V", ctx.object());

    QJniObject::callStaticMethod<void>(
        kPlatformClass, "requestPermission",
        "(Landroid/content/Context;)V", ctx.object());
}

bool platformNotify(const QString& title, const QString& body)
{
    const QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return false;

    const QJniObject jTitle = QJniObject::fromString(title);
    const QJniObject jBody  = QJniObject::fromString(body);

    QJniObject::callStaticMethod<void>(
        kPlatformClass, "showNotification",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
        ctx.object(), jTitle.object<jstring>(), jBody.object<jstring>());

    return true;
}

bool platformShare(const QString& text)
{
    const QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return false;

    const QJniObject jText = QJniObject::fromString(text);
    return QJniObject::callStaticMethod<jboolean>(
        kPlatformClass, "shareText",
        "(Landroid/content/Context;Ljava/lang/String;)Z",
        ctx.object(), jText.object<jstring>());
}

#else // !Q_OS_ANDROID

void initNotifications() {}

bool platformNotify(const QString&, const QString&) { return false; }

bool platformShare(const QString&) { return false; }

#endif

} // namespace app
