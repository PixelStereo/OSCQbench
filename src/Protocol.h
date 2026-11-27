#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>

namespace bench {

QString json(const QJsonValue &value);
QStringList validate(const QJsonObject &root);
QByteArray packet(const QString &path, const QString &tags, const QString &arguments, QString &error);
QString packetSummary(const QByteArray &packet);

} // namespace bench
