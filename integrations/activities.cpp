// SPDX-FileCopyrightText: 2026 Chris Burton <code@chrisburton.uk>
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "core.h"
#include "entities/sensor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <QDebug>
#include <QLoggingCategory>
#include <QObject>

Q_DECLARE_LOGGING_CATEGORY(activities)
Q_LOGGING_CATEGORY(activities, "integration.Activities")

class ActivitiesIntegration : public QObject
{
    Q_OBJECT
public:
    explicit ActivitiesIntegration(QObject *parent = nullptr);
    ~ActivitiesIntegration() override = default;

private slots:
    void updateCurrentActivity();

private:
    void handleCurrentActivityReply(QDBusPendingCallWatcher *watcher);
    void handleActivityNameReply(QDBusPendingCallWatcher *watcher);

    Sensor *m_sensor_name = nullptr;
    Sensor *m_sensor_uuid = nullptr;
    QDBusConnection m_dbusConnection;
    QString m_currentActivityUuid;
    quint64 m_requestGeneration = 0;
};

ActivitiesIntegration::ActivitiesIntegration(QObject *parent)
    : QObject(parent)
    , m_sensor_name(new Sensor(this))
    , m_sensor_uuid(new Sensor(this))
    , m_dbusConnection(QDBusConnection::sessionBus())
{
    m_sensor_name->setObjectName(QStringLiteral("activity_name"));
    m_sensor_name->setId(QStringLiteral("activity_name"));
    m_sensor_name->setName(QStringLiteral("Activity Name"));

    m_sensor_uuid->setObjectName(QStringLiteral("activity_id"));
    m_sensor_uuid->setId(QStringLiteral("activity_id"));
    m_sensor_uuid->setName(QStringLiteral("Activity Id"));

    if (!m_dbusConnection.isConnected()) {
        qWarning(activities) << "Failed to connect to D-Bus session bus.";
        return;
    }

    const bool connected = m_dbusConnection.connect(QStringLiteral("org.kde.ActivityManager"),
                                                    QStringLiteral("/ActivityManager/Activities"),
                                                    QStringLiteral("org.kde.ActivityManager.Activities"),
                                                    QStringLiteral("CurrentActivityChanged"),
                                                    this,
                                                    SLOT(updateCurrentActivity()));

    if (!connected) {
        qWarning(activities) << "Failed to connect to CurrentActivityChanged signal.";
        return;
    }

    updateCurrentActivity();
}

void ActivitiesIntegration::updateCurrentActivity()
{
    if (!m_dbusConnection.isConnected()) {
        return;
    }

    const quint64 generation = ++m_requestGeneration;

    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.ActivityManager"),
                                                          QStringLiteral("/ActivityManager/Activities"),
                                                          QStringLiteral("org.kde.ActivityManager.Activities"),
                                                          QStringLiteral("CurrentActivity"));

    auto *watcher = new QDBusPendingCallWatcher(m_dbusConnection.asyncCall(message), this);

    watcher->setProperty("generation", QVariant::fromValue(generation));

    connect(watcher, &QDBusPendingCallWatcher::finished, this, &ActivitiesIntegration::handleCurrentActivityReply);
}

void ActivitiesIntegration::handleCurrentActivityReply(QDBusPendingCallWatcher *watcher)
{
    const quint64 generation = watcher->property("generation").toULongLong();
    QDBusPendingReply<QString> reply = *watcher;
    watcher->deleteLater();

    if (generation != m_requestGeneration) {
        return;
    }

    if (reply.isError()) {
        qWarning(activities) << "Error getting current activity UUID:" << reply.error().message();
        return;
    }

    const QString newUuid = reply.value();
    if (newUuid.isEmpty()) {
        return;
    }

    m_currentActivityUuid = newUuid;

    QDBusMessage nameMessage = QDBusMessage::createMethodCall(QStringLiteral("org.kde.ActivityManager"),
                                                              QStringLiteral("/ActivityManager/Activities"),
                                                              QStringLiteral("org.kde.ActivityManager.Activities"),
                                                              QStringLiteral("ActivityName"));
    nameMessage << m_currentActivityUuid;

    auto *nameWatcher = new QDBusPendingCallWatcher(m_dbusConnection.asyncCall(nameMessage), this);

    nameWatcher->setProperty("generation", QVariant::fromValue(generation));

    connect(nameWatcher, &QDBusPendingCallWatcher::finished, this, &ActivitiesIntegration::handleActivityNameReply);
}

void ActivitiesIntegration::handleActivityNameReply(QDBusPendingCallWatcher *watcher)
{
    const quint64 generation = watcher->property("generation").toULongLong();
    QDBusPendingReply<QString> reply = *watcher;
    watcher->deleteLater();

    if (generation != m_requestGeneration) {
        return;
    }

    m_sensor_uuid->setState(m_currentActivityUuid);

    if (reply.isError()) {
        qWarning(activities) << "Error getting activity name for UUID" << m_currentActivityUuid << ":" << reply.error().message();
        m_sensor_name->setState(QStringLiteral("Unknown Activity"));
        return;
    }

    m_sensor_name->setState(reply.value());

    qDebug(activities) << "Activity set to" << m_sensor_name->state() << m_sensor_uuid->state();
}

static ActivitiesIntegration *activitiesIntegration = nullptr;

static void setupActivitiesIntegration()
{
    activitiesIntegration = new ActivitiesIntegration(qApp);
}

REGISTER_INTEGRATION("Activities", setupActivitiesIntegration, true)

#include "activities.moc"
