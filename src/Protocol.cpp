#include "Protocol.h"

#include <pxs/core/osc/codec.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace bench {
namespace {

using namespace pxs::core::osc;

struct Type {
    QChar tag;
    QList<Type> children;
};

bool readTypes(const QString &tags, qsizetype &pos, QList<Type> &out, int depth)
{
    if (depth > 32) return false;
    const QString known = QStringLiteral("ifsbhtdScrmTFNI");
    while (pos < tags.size()) {
        const QChar tag = tags[pos++];
        if (tag == QLatin1Char(']')) return depth > 0;
        Type type{tag, {}};
        if (tag == QLatin1Char('[')) {
            if (!readTypes(tags, pos, type.children, depth + 1)) return false;
        } else if (!known.contains(tag)) {
            return false;
        }
        out.append(type);
    }
    return depth == 0;
}

bool byteHex(const QString &s, QByteArray &bytes)
{
    static const QRegularExpression hex(QStringLiteral("^(?:[a-fA-F0-9]{2})*$"));
    if (!hex.match(s).hasMatch()) return false;
    bytes = QByteArray::fromHex(s.toLatin1());
    return true;
}

bool integer(const QJsonValue &v, qlonglong &value)
{
    if (v.isString()) {
        const QString text = v.toString();
        bool ok = false;
        value = text.startsWith("0x", Qt::CaseInsensitive) ? text.mid(2).toLongLong(&ok, 16) : text.toLongLong(&ok);
        return ok;
    }
    const double n = v.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(n) || std::trunc(n) != n || n < -9007199254740991.0 || n > 9007199254740991.0) return false;
    value = qlonglong(n);
    return true;
}

bool arguments(const QList<Type> &types, const QJsonArray &values, Message &message)
{
    if (values.size() != types.size()) return false;
    for (qsizetype i = 0; i < types.size(); ++i) {
        const Type &t = types[i];
        const QJsonValue v = values[i];
        message.types += t.tag.toLatin1();
        qlonglong n = 0;
        switch (t.tag.toLatin1()) {
        case '[':
            message.args.emplace_back();
            if (!v.isArray() || !arguments(t.children, v.toArray(), message)) return false;
            message.types += ']';
            message.args.emplace_back();
            break;
        case 'i': case 'h': case 't':
            if (!integer(v, n)) {
                if (t.tag != QLatin1Char('t') || !v.isString() || !v.toString().startsWith("0x", Qt::CaseInsensitive)) return false;
                bool ok = false;
                const quint64 bits = v.toString().mid(2).toULongLong(&ok, 16);
                if (!ok) return false;
                n = qlonglong(bits);
            }
            if (t.tag == QLatin1Char('i')) {
                if (n < std::numeric_limits<std::int32_t>::min() || n > std::numeric_limits<std::int32_t>::max()) return false;
                message.args.emplace_back(std::int32_t(n));
            } else message.args.emplace_back(std::int64_t(n));
            break;
        case 'f': case 'd':
            if (!v.isDouble() || !std::isfinite(v.toDouble())) return false;
            if (t.tag == QLatin1Char('f')) {
                if (std::abs(v.toDouble()) > std::numeric_limits<float>::max()) return false;
                message.args.emplace_back(float(v.toDouble()));
            } else message.args.emplace_back(v.toDouble());
            break;
        case 's': case 'S':
            if (!v.isString() || v.toString().contains(QChar(0))) return false;
            message.args.emplace_back(v.toString().toStdString());
            break;
        case 'c': {
            const auto letters = v.toString().toUcs4();
            if (letters.size() != 1) return false;
            message.args.emplace_back(std::uint32_t(letters.front()));
            break;
        }
        case 'b': {
            QByteArray bytes;
            if (!v.isString() || !byteHex(v.toString(), bytes)) return false;
            message.args.emplace_back(Blob(bytes.begin(), bytes.end()));
            break;
        }
        case 'r': case 'm':
            if (t.tag == QLatin1Char('r') && v.isString() && v.toString().startsWith('#')) {
                QByteArray bytes;
                if (!byteHex(v.toString().mid(1), bytes) || bytes.size() != 4) return false;
                n = v.toString().mid(1).toLongLong(nullptr, 16);
            } else if (!integer(v, n)) return false;
            if (n < 0 || n > 0xffffffffLL) return false;
            message.args.emplace_back(std::uint32_t(n));
            break;
        case 'T': case 'F':
            if (!v.isBool() || v.toBool() != (t.tag == QLatin1Char('T'))) return false;
            message.args.emplace_back(v.toBool());
            break;
        case 'N': case 'I':
            if (!v.isNull()) return false;
            message.args.emplace_back();
            break;
        }
    }
    return true;
}

bool attrShape(const QList<Type> &types, const QJsonArray &values, const QString &attribute)
{
    if (values.size() != types.size()) return false;
    const QStringList clips{"none", "low", "high", "both"};
    for (qsizetype i = 0; i < types.size(); ++i) {
        const Type &type = types[i];
        const QJsonValue value = values[i];
        if (value.isNull()) continue;
        if (type.tag == QLatin1Char('[')) {
            if (!value.isArray() || !attrShape(type.children, value.toArray(), attribute)) return false;
        } else if (attribute == "RANGE") {
            if (!value.isObject()) return false;
            const auto bounds = value.toObject();
            const auto lo = bounds.value("MIN"), hi = bounds.value("MAX");
            if (lo.isDouble() && hi.isDouble() && lo.toDouble() > hi.toDouble()) return false;
            if (bounds.contains("VALS") && !bounds.value("VALS").isArray()) return false;
        } else if (attribute == "CLIPMODE") {
            if (!clips.contains(value.toString())) return false;
        } else if (attribute == "UNIT" || attribute == "EXTENDED_TYPE") {
            if (!value.isString()) return false;
        } else if (type.tag == QLatin1Char('r')) {
            QByteArray bytes;
            if (!value.toString().startsWith('#') || !byteHex(value.toString().mid(1), bytes) || bytes.size() != 4) return false;
        } else if (QStringLiteral("ifhdt").contains(type.tag) && !value.isDouble()) return false;
        else if (QStringLiteral("sSc").contains(type.tag) && !value.isString()) return false;
        else if (QStringLiteral("TF").contains(type.tag) && !value.isBool()) return false;
        else if (QStringLiteral("NIbm").contains(type.tag)) return false;
    }
    return true;
}

void inspect(const QJsonObject &node, const QString &path, QStringList &out, int depth)
{
    if (out.size() >= 200) return;
    const auto issue = [&](const QString &detail) { out << path + "  " + detail; };
    if (depth > 64) { issue("Namespace depth exceeds the inspection limit"); return; }
    if (node.value("FULL_PATH").toString() != path) issue("FULL_PATH differs from its location in CONTENTS");
    if (node.contains("ACCESS")) {
        const int access = node.value("ACCESS").toInt(-1);
        if (access < 0 || access > 3) issue("ACCESS must be an integer from 0 to 3");
    }
    if (node.contains("TYPE")) {
        const auto type = node.value("TYPE");
        QList<Type> layout;
        qsizetype pos = 0;
        if (!type.isString() || !readTypes(type.toString(), pos, layout, 0)) issue("TYPE contains unknown tags or unbalanced arrays");
        else {
            for (const QString attribute : {"VALUE", "RANGE", "CLIPMODE", "UNIT", "EXTENDED_TYPE"}) {
                if (!node.contains(attribute)) continue;
                const auto value = node.value(attribute);
                auto values = value.toArray();
                if (!value.isArray() && attribute != "VALUE")
                    for (qsizetype i = 0; i < layout.size(); ++i) values.append(value);
                if ((!value.isArray() && attribute == "VALUE") || !attrShape(layout, values, attribute))
                    issue(attribute + " does not match TYPE or contains invalid values");
            }
        }
    }
    const auto children = node.value("CONTENTS");
    if (!children.isUndefined() && !children.isObject()) issue("CONTENTS must be an object");
    const auto childNodes = children.toObject();
    for (auto it = childNodes.begin(); it != childNodes.end() && out.size() < 200; ++it) {
        const QString childPath = (path == "/" ? QString() : path) + "/" + it.key();
        if (it.value().isObject()) inspect(it.value().toObject(), childPath, out, depth + 1);
        else out << childPath + "  CONTENTS child must be an object";
    }
}

} // namespace

QString json(const QJsonValue &value)
{
    QByteArray bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(bytes.mid(1, bytes.size() - 2));
}

QStringList validate(const QJsonObject &root)
{
    QStringList issues;
    inspect(root, "/", issues, 0);
    return issues;
}

QByteArray packet(const QString &path, const QString &tags, const QString &input, QString &error)
{
    error.clear();
    if (!path.startsWith('/') || path.contains(QChar(0))) {
        error = "Enter an OSC address beginning with /.";
        return {};
    }
    QList<Type> layout;
    qsizetype pos = 0;
    if (!readTypes(tags, pos, layout, 0)) {
        error = "Use OSC 1.0 type tags with balanced array brackets.";
        return {};
    }
    const auto values = QJsonDocument::fromJson(input.toUtf8());
    Message message;
    message.address = path.toStdString();
    if (!values.isArray() || !arguments(layout, values.array(), message)) {
        error = "The JSON arguments must match the tags. Use null for N/I and strings for exact 64-bit integers.";
        return {};
    }
    const auto wire = encode(message);
    return QByteArray(reinterpret_cast<const char *>(wire.data()), qsizetype(wire.size()));
}

QString packetSummary(const QByteArray &packet)
{
    std::vector<Message> messages;
    if (!decode(reinterpret_cast<const std::uint8_t *>(packet.data()), std::size_t(packet.size()), messages))
        return "Unrecognized OSC";
    QStringList descriptions;
    for (const auto &message : messages) descriptions << QString::fromStdString(message.address + " ," + message.types);
    return descriptions.join(" · ");
}

} // namespace bench
