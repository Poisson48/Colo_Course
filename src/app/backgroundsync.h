#pragma once

#include <QObject>

namespace app {

// Synchro courte sans UI, exécutée dans le process Android :qt_service.
class BackgroundSync : public QObject
{
    Q_OBJECT
public:
    explicit BackgroundSync(QObject* parent = nullptr);
    bool start();
};

} // namespace app
