#pragma once

#include <pxs/Discovery.h>

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QUdpSocket>
#include <QUrl>

class Device;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;
#ifdef PXS_HAS_WEBSOCKETS
class QWebSocket;
#endif
namespace pxs { class OscService; }

class BenchWindow final : public QMainWindow
{
public:
    BenchWindow(Device &device, pxs::OscService &service);
    ~BenchWindow() override;

private:
    void connectTarget();
    void get(const QString &path, const QString &attribute = {});
    void showTree(const QJsonObject &root);
    void select(QTreeWidgetItem *item);
    void trace(const QString &channel, const QString &message);
    void send();
    void sendWire(const QByteArray &wire, bool websocket);
    void sendControl(const QString &path, const QString &tags, const QString &arguments);
    void command(const QString &name, const QString &path);
    void showControls(QTreeWidgetItem *item);
    void showFeedback(const QString &path, const QString &value);
    void openWebSocket();

    Device &m_device;
    int m_observer = 0;
    QHash<QString, QLabel *> m_controlValues;
    QNetworkAccessManager m_http;
    QUdpSocket m_udp;
    pxs::oscquery::Discovery m_discovery;
    QUrl m_local, m_target;
    QJsonObject m_host;
    quint64 m_generation = 0;
    bool m_hostPending = false;
    bool m_refreshing = false;
    bool m_refreshPending = false;
#ifdef PXS_HAS_WEBSOCKETS
    QWebSocket *m_ws;
#endif
    QComboBox *m_url, *m_attribute, *m_transport;
    QLabel *m_status, *m_summary;
    QLineEdit *m_filter, *m_path, *m_tags, *m_requestPath;
    QTreeWidget *m_tree;
    QScrollArea *m_controls;
    QPlainTextEdit *m_details, *m_response, *m_report, *m_trace, *m_arguments;
};
