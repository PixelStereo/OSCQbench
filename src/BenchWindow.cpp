#include "BenchWindow.h"

#include "Device.h"
#include "Protocol.h"

#include <OscCodec.h>
#include <pxs/Convert.h>
#include <pxs/OscService.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDateTime>
#include <QHeaderView>
#include <QFontDatabase>
#include <QSet>
#include <functional>
#include <QHBoxLayout>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkReply>
#include <QNetworkDatagram>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#ifdef PXS_HAS_WEBSOCKETS
#include <QWebSocket>
#endif

namespace {
QPlainTextEdit *editor(bool readOnly = true)
{
    auto *view = new QPlainTextEdit;
    view->setReadOnly(readOnly);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    return view;
}

QPushButton *button(QBoxLayout *layout, const QString &label)
{
    auto *control = new QPushButton(label);
    layout->addWidget(control);
    return control;
}

QString jsonObject(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

QString access(const QJsonObject &node)
{
    switch (node.value("ACCESS").toInt(-1)) {
    case 0: return "—";
    case 1: return "Read";
    case 2: return "Write";
    case 3: return "Read / write";
    default: return "Unspecified";
    }
}

QStringList defaults(const QString &tags)
{
    QStringList values;
    for (const QChar tag : tags) {
        if (tag == '[') { values << "["; continue; }
        if (tag == ']') { values << "]"; continue; }
        if (tag == 'T') values << "true";
        else if (tag == 'F') values << "false";
        else if (tag == 'N' || tag == 'I') values << "null";
        else if (tag == 'c') values << "\"A\"";
        else if (tag == 'r') values << "\"#FF8040C0\"";
        else if (tag == 's' || tag == 'S' || tag == 'b') values << "\"\"";
        else values << "0";
    }
    return values;
}

} // namespace

BenchWindow::BenchWindow(Device &device, pxs::OscService &service)
    : m_device(device), m_local(QString("http://127.0.0.1:%1").arg(service.httpPort()))
#ifdef PXS_HAS_WEBSOCKETS
    , m_ws(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
#endif
{
    setWindowTitle("OSCQbench");
    resize(1280, 840);
    setMinimumSize(720, 520);
    auto *main = new QWidget;
    auto *body = new QVBoxLayout(main);
    body->setContentsMargins(18, 16, 18, 12);
    body->setSpacing(10);
    setCentralWidget(main);

    auto *heading = new QLabel("OSCQbench");
    auto font = heading->font();
    font.setPointSize(font.pointSize() + 8);
    font.setBold(true);
    heading->setFont(font);
    body->addWidget(heading);

    auto *connection = new QHBoxLayout;
    connection->addWidget(new QLabel("OSCQuery endpoint"));
    m_url = new QComboBox;
    m_url->setEditable(true);
    m_url->addItem(m_local.toString());
    m_url->lineEdit()->setPlaceholderText("http://host:5678");
    connection->addWidget(m_url, 1);
    auto *open = button(connection, "Connect");
    auto *local = button(connection, "Local fixture");
    auto *refresh = button(connection, "Refresh");
    body->addLayout(connection);
    m_status = new QLabel;
    m_status->setObjectName("endpointStatus");
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->addWidget(m_status);

    auto *vertical = new QSplitter(Qt::Vertical);
    vertical->setChildrenCollapsible(false);
    body->addWidget(vertical, 1);
    auto *top = new QSplitter(Qt::Horizontal);
    top->setChildrenCollapsible(false);
    vertical->addWidget(top);
    auto *browser = new QWidget;
    auto *browse = new QVBoxLayout(browser);
    browse->setContentsMargins(0, 0, 0, 0);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText("Filter by path or OSC tag");
    browse->addWidget(m_filter);
    m_tree = new QTreeWidget;
    m_tree->setObjectName("namespace");
    m_tree->setHeaderLabels({"Address", "Type", "Access", "Value"});
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->setColumnWidth(3, 120);
    browse->addWidget(m_tree);
    top->addWidget(browser);

    auto *inspector = new QTabWidget;
    m_controls = new QScrollArea;
    m_controls->setObjectName("publishedControls");
    m_controls->setWidgetResizable(true);
    inspector->addTab(m_controls, "Controls");
    auto *properties = new QWidget;
    auto *inspect = new QVBoxLayout(properties);
    m_summary = new QLabel("Select a node");
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    inspect->addWidget(m_summary);
    m_details = editor();
    inspect->addWidget(m_details);
    inspector->addTab(properties, "Node JSON");

    auto *query = new QWidget;
    auto *queryLayout = new QVBoxLayout(query);
    auto *request = new QHBoxLayout;
    m_requestPath = new QLineEdit("/");
    m_requestPath->setPlaceholderText("OSCQuery path");
    request->addWidget(m_requestPath, 1);
    m_attribute = new QComboBox;
    m_attribute->setEditable(true);
    m_attribute->addItems({"", "HOST_INFO", "TYPE", "VALUE", "ACCESS", "CONTENTS", "DESCRIPTION", "RANGE",
        "DEFAULT_VALUE", "CLIPMODE", "UNIT", "TAGS", "EXTENDED_TYPE", "CRITICAL", "OVERLOADS", "HTML"});
    request->addWidget(m_attribute);
    auto *fetch = button(request, "GET");
    queryLayout->addLayout(request);
    m_response = editor();
    queryLayout->addWidget(m_response);
    inspector->addTab(query, "HTTP probe");
    m_report = editor();
    inspector->addTab(m_report, "Conformance");
    top->addWidget(inspector);
    top->setSizes({540, 660});

    auto *bottom = new QSplitter(Qt::Horizontal);
    bottom->setChildrenCollapsible(false);
    vertical->addWidget(bottom);
    auto *compose = new QWidget;
    auto *sender = new QVBoxLayout(compose);
    sender->setContentsMargins(0, 0, 0, 0);
    auto *fields = new QHBoxLayout;
    m_path = new QLineEdit("/");
    m_path->setPlaceholderText("OSC address");
    m_tags = new QLineEdit;
    m_tags->setPlaceholderText("Tags");
    m_tags->setMaximumWidth(110);
    m_transport = new QComboBox;
    m_transport->addItems({"OSC / UDP", "Raw / UDP"});
#ifdef PXS_HAS_WEBSOCKETS
    m_transport->addItems({"OSC / WebSocket", "Raw / WebSocket"});
#endif
    fields->addWidget(m_path, 1);
    fields->addWidget(m_tags);
    fields->addWidget(m_transport);
    auto *sendPacket = button(fields, "Send");
    sender->addLayout(fields);
    m_arguments = editor(false);
    m_arguments->setPlainText("[]");
    m_arguments->setPlaceholderText("JSON arguments or raw packet hex");
    sender->addWidget(m_arguments);
    auto *stream = new QHBoxLayout;
    auto *ws = button(stream, "Open WS");
    auto *listen = button(stream, "LISTEN");
    auto *ignore = button(stream, "IGNORE");
    auto *changeFixture = button(stream, "Toggle fixture path");
    auto *pulseFixture = button(stream, "Emit fixture signal");
    stream->addStretch();
    sender->addLayout(stream);
    bottom->addWidget(compose);
    auto *traffic = new QWidget;
    auto *trafficLayout = new QVBoxLayout(traffic);
    trafficLayout->setContentsMargins(0, 0, 0, 0);
    auto *trafficHeading = new QHBoxLayout;
    trafficHeading->addWidget(new QLabel("Traffic"));
    trafficHeading->addStretch();
    auto *clear = button(trafficHeading, "Clear");
    trafficLayout->addLayout(trafficHeading);
    m_trace = editor();
    m_trace->setMaximumBlockCount(1200);
    trafficLayout->addWidget(m_trace);
    bottom->addWidget(traffic);
    bottom->setSizes({540, 660});
    vertical->setSizes({510, 240});

    connect(open, &QPushButton::clicked, this, &BenchWindow::connectTarget);
    connect(m_url->lineEdit(), &QLineEdit::returnPressed, this, &BenchWindow::connectTarget);
    connect(local, &QPushButton::clicked, this, [this] { m_url->setCurrentText(m_local.toString()); connectTarget(); });
    connect(refresh, &QPushButton::clicked, this, [this] { get("/"); });
    connect(fetch, &QPushButton::clicked, this, [this] { get(m_requestPath->text(), m_attribute->currentText().trimmed()); });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &BenchWindow::select);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        std::function<bool(QTreeWidgetItem *)> filter = [&](QTreeWidgetItem *item) {
            const auto node = item->data(0, Qt::UserRole).toJsonObject();
            bool match = node.value("FULL_PATH").toString().contains(text, Qt::CaseInsensitive)
                          || item->text(1).contains(text, Qt::CaseInsensitive);
            for (int i = 0; i < item->childCount(); ++i) match |= filter(item->child(i));
            item->setHidden(!match);
            if (match && !text.isEmpty()) item->setExpanded(true);
            return match;
        };
        if (m_tree->topLevelItemCount()) filter(m_tree->topLevelItem(0));
    });
    connect(sendPacket, &QPushButton::clicked, this, &BenchWindow::send);
    connect(ws, &QPushButton::clicked, this, &BenchWindow::openWebSocket);
    connect(listen, &QPushButton::clicked, this, [this] { command("LISTEN", m_path->text()); });
    connect(ignore, &QPushButton::clicked, this, [this] { command("IGNORE", m_path->text()); });
    connect(clear, &QPushButton::clicked, m_trace, &QPlainTextEdit::clear);
    connect(changeFixture, &QPushButton::clicked, this, [&device] { device.toggleProbePath(); });
    connect(pulseFixture, &QPushButton::clicked, this, [&device] { device.emitProbeSignal(); });
    connect(&service, &pxs::OscService::messageReceived, this, [this](const ::osc::Message &message, int accepted) {
        trace("LOCAL OSC IN", QString("%1 ,%2 · %3").arg(message.address, message.types, accepted ? "accepted" : "refused"));
    });
    connect(&m_discovery, &pxs::oscquery::Discovery::deviceFound, this, [this](const pxs::oscquery::Device &found) {
        QUrl endpoint;
        endpoint.setScheme("http");
        endpoint.setHost(found.address.toString());
        endpoint.setPort(found.port);
        if (m_url->findData(endpoint.toString()) < 0)
            m_url->addItem(found.name + " · " + endpoint.toString(), endpoint.toString());
    });
    connect(&m_discovery, &pxs::oscquery::Discovery::deviceLost, this, [this](const pxs::oscquery::Device &found) {
        QUrl endpoint;
        endpoint.setScheme("http");
        endpoint.setHost(found.address.toString());
        endpoint.setPort(found.port);
        const int index = m_url->findData(endpoint.toString());
        if (index >= 0) m_url->removeItem(index);
    });
    connect(&m_udp, &QUdpSocket::readyRead, this, [this] {
        while (m_udp.hasPendingDatagrams()) {
            const auto datagram = m_udp.receiveDatagram();
            const auto bytes = datagram.data();
            trace("UDP IN", QString("%1:%2  %3  %4").arg(datagram.senderAddress().toString()).arg(datagram.senderPort())
                .arg(bench::packetSummary(bytes), QString::fromLatin1(bytes.toHex(' '))));
        }
    });
    if (!m_discovery.start()) trace("mDNS", "Discovery unavailable. Enter an endpoint manually.");
#ifdef PXS_HAS_WEBSOCKETS
    connect(m_ws, &QWebSocket::connected, this, [this] { trace("WS", "Connected"); });
    connect(m_ws, &QWebSocket::disconnected, this, [this] { trace("WS", "Disconnected"); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const auto wsError = &QWebSocket::errorOccurred;
#else
    const auto wsError = qOverload<QAbstractSocket::SocketError>(&QWebSocket::error);
#endif
    connect(m_ws, wsError, this, [this](QAbstractSocket::SocketError) { trace("WS", m_ws->errorString()); });
    connect(m_ws, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
        trace("WS IN", message);
        const auto command = QJsonDocument::fromJson(message.toUtf8()).object().value("COMMAND").toString();
        if (command.startsWith("PATH_")) get("/");
    });
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray &packet) {
        trace("WS IN", bench::packetSummary(packet) + "  " + packet.toHex(' '));
        std::vector<::osc::Message> messages;
        if (!::osc::decode(packet, messages)) return;
        for (const auto &message : messages) {
            QStringList values;
            for (const QVariant &value : message.args) values << value.toString();
            showFeedback(message.address, values.isEmpty() ? QString(",") + message.types : values.join(", "));
        }
    });
#endif
    m_observer = device.tree().addObserver([this](const auto &node, const auto &args) {
        if (m_target != m_local) return;
        QJsonArray values;
        for (const auto &value : args) values.append(pxs::convert::toJson(value));
        showFeedback(QString::fromStdString(node.path()), bench::json(values));
    });
    connectTarget();
}

BenchWindow::~BenchWindow()
{
    m_device.tree().removeObserver(m_observer);
}

void BenchWindow::connectTarget()
{
    auto text = m_url->currentText().trimmed();
    if (m_url->currentIndex() >= 0 && text == m_url->itemText(m_url->currentIndex())
        && m_url->currentData().isValid()) text = m_url->currentData().toString();
    if (!text.contains("://")) text.prepend("http://");
    const QUrl endpoint(text);
    if (!endpoint.isValid() || endpoint.host().isEmpty() || (endpoint.scheme() != "http" && endpoint.scheme() != "https")) {
        m_status->setText("Enter an HTTP or HTTPS OSCQuery endpoint.");
        return;
    }
    m_target = endpoint;
    m_host = {};
    m_hostPending = true;
    ++m_generation;
    m_refreshing = false;
    m_refreshPending = false;
#ifdef PXS_HAS_WEBSOCKETS
    m_ws->abort();
#endif
    m_status->setText("Connecting to " + m_target.toDisplayString());
    m_tree->clear();
    m_controlValues.clear();
    delete m_controls->takeWidget();
    m_details->clear();
    m_response->clear();
    m_report->clear();
    get("/", "HOST_INFO");
    get("/");
}

void BenchWindow::get(const QString &path, const QString &attribute)
{
    if (m_target.isEmpty() || !path.startsWith('/')) {
        trace("HTTP", "Enter an absolute OSCQuery path.");
        return;
    }
    const bool tree = path == "/" && attribute.isEmpty();
    if (tree && m_refreshing) { m_refreshPending = true; return; }
    if (tree) m_refreshing = true;
    QUrl url = m_target;
    url.setPath(path);
    url.setQuery(attribute);
    QNetworkRequest request(url);
    request.setTransferTimeout(7000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_http.get(request);
    const auto generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, url, attribute, tree] {
        reply->deleteLater();
        if (generation != m_generation) return;
        if (attribute == "HOST_INFO") m_hostPending = false;
        if (tree) {
            m_refreshing = false;
            if (m_refreshPending) {
                m_refreshPending = false;
                get("/");
            }
        }
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const auto document = QJsonDocument::fromJson(data);
        const auto object = document.object();
        const auto line = QString("%1 · %2").arg(code).arg(url.toDisplayString());
        trace("HTTP", code ? line : reply->errorString());
        m_response->setPlainText(line + "\n\n" + (document.isNull() ? QString::fromUtf8(data) : QString::fromUtf8(document.toJson())));
        if (code != 200 || !document.isObject()) {
            if (tree) m_status->setText("Namespace unavailable · " + (code ? line : reply->errorString()));
            return;
        }
        if (attribute == "HOST_INFO") {
            m_host = object;
            const int udp = m_host.value("OSC_PORT").toInt(m_target.port(m_target.scheme() == "https" ? 443 : 80));
            m_status->setText(QString("%1 · HTTP %2 · OSC %3:%4 (%5)").arg(m_host.value("NAME").toString(m_target.host()),
                m_target.toDisplayString(), m_host.value("OSC_IP").toString(m_target.host())).arg(udp)
                .arg(m_host.value("OSC_TRANSPORT").toString("UDP")));
            if (m_host.contains("WS_PORT") || m_host.value("EXTENSIONS").toObject().contains("LISTEN")) openWebSocket();
        } else if (tree) {
            showTree(object);
            const auto issues = bench::validate(object);
            m_report->setPlainText(issues.isEmpty() ? "No issues in the checked OSCQuery attributes."
                : QString("%1 findings\n\n").arg(issues.size()) + issues.join('\n'));
            if (m_host.isEmpty()) m_status->setText("Namespace loaded · " + m_target.toDisplayString());
        }
    });
}

void BenchWindow::showTree(const QJsonObject &root)
{
    const QString selected = m_tree->currentItem() ? m_tree->currentItem()->data(0, Qt::UserRole).toJsonObject().value("FULL_PATH").toString() : "/";
    QSet<QString> expanded;
    for (QTreeWidgetItemIterator iterator(m_tree); *iterator; ++iterator)
        if ((*iterator)->isExpanded()) expanded.insert((*iterator)->data(0, Qt::UserRole).toJsonObject().value("FULL_PATH").toString());
    m_tree->clear();
    QTreeWidgetItem *restore = nullptr;
    std::function<void(QTreeWidgetItem *, const QString &, QJsonObject, int)> add =
        [&](QTreeWidgetItem *parent, const QString &name, QJsonObject node, int depth) {
            if (depth > 64) return;
            const auto children = node.take("CONTENTS").toObject();
            const auto path = node.value("FULL_PATH").toString();
            const auto value = node.value("VALUE");
            auto *item = new QTreeWidgetItem(QStringList{name, node.value("TYPE").toString(), access(node),
                value.isUndefined() ? QString() : bench::json(value).left(110)});
            if (parent) parent->addChild(item);
            else m_tree->addTopLevelItem(item);
            item->setData(0, Qt::UserRole, node);
            item->setToolTip(0, path);
            if (path == selected) restore = item;
            for (auto it = children.begin(); it != children.end(); ++it)
                if (it.value().isObject()) add(item, it.key(), it.value().toObject(), depth + 1);
            if (depth == 0 || expanded.contains(path)) item->setExpanded(true);
        };
    add(nullptr, "/", root, 0);
    m_tree->setCurrentItem(restore ? restore : m_tree->topLevelItem(0));
    if (!m_filter->text().isEmpty()) m_filter->textChanged(m_filter->text());
}

void BenchWindow::select(QTreeWidgetItem *item)
{
    if (!item) return;
    const auto node = item->data(0, Qt::UserRole).toJsonObject();
    const auto path = node.value("FULL_PATH").toString();
    const auto tags = node.value("TYPE").toString();
    m_summary->setText(path + "    " + tags + "    " + access(node));
    m_details->setPlainText(jsonObject(node));
    m_requestPath->setText(path);
    m_path->setText(path);
    m_tags->setText(tags);
    const auto values = node.value("VALUE").toArray();
    QString input = bench::json(values);
    QString error;
    bench::packet(path, tags, input, error);
    if (!error.isEmpty()) {
        input = "[" + defaults(tags).join(",").replace("[,", "[").replace(",]", "]") + "]";
    }
    m_arguments->setPlainText(input);
    showControls(item);
}

void BenchWindow::showControls(QTreeWidgetItem *selected)
{
    m_controlValues.clear();
    delete m_controls->takeWidget();
    auto *panel = new QWidget;
    auto *rows = new QVBoxLayout(panel);
    rows->setAlignment(Qt::AlignTop);
    rows->setSpacing(8);
    int controls = 0;
    std::function<void(QTreeWidgetItem *)> add = [&](QTreeWidgetItem *item) {
        if (controls >= 200) return;
        const auto node = item->data(0, Qt::UserRole).toJsonObject();
        const auto path = node.value("FULL_PATH").toString();
        const auto tags = node.value("TYPE").toString();
        const auto value = node.value("VALUE").toArray();
        const int rights = node.value("ACCESS").toInt(tags.isEmpty() ? 0 : 3);
        if (rights || !tags.isEmpty() || node.contains("TYPE")) {
            ++controls;
            auto *line = new QHBoxLayout;
            auto *name = new QLabel(path + "  " + tags);
            name->setTextInteractionFlags(Qt::TextSelectableByMouse);
            name->setToolTip(node.value("DESCRIPTION").toString());
            line->addWidget(name, 2);
            if (rights & 2) {
                if (tags.isEmpty() || tags == "N" || tags == "I") {
                    auto *fire = button(line, "Trigger");
                    connect(fire, &QPushButton::clicked, this, [this, path, tags] {
                        sendControl(path, tags, tags.isEmpty() ? "[]" : "[null]");
                    });
                } else if (tags == "T" || tags == "F") {
                    auto *toggle = new QCheckBox;
                    toggle->setChecked(value.at(0).toBool());
                    line->addWidget(toggle);
                    connect(toggle, &QCheckBox::toggled, this, [this, path](bool on) {
                        sendControl(path, on ? "T" : "F", on ? "[true]" : "[false]");
                    });
                } else if (tags == "f" || tags == "d" || tags == "i") {
                    auto *number = new QDoubleSpinBox;
                    number->setDecimals(tags == "i" ? 0 : 6);
                    number->setRange(-2147483648.0, 2147483647.0);
                    number->setKeyboardTracking(false);
                    number->setValue(value.at(0).toDouble());
                    line->addWidget(number);
                    auto *apply = button(line, "Send");
                    connect(apply, &QPushButton::clicked, this, [this, path, tags, number] {
                        sendControl(path, tags, bench::json(QJsonArray{number->value()}));
                    });
                } else {
                    auto *input = new QLineEdit;
                    QString args = bench::json(value), error;
                    bench::packet(path, tags, args, error);
                    if (!error.isEmpty()) args = "[" + defaults(tags).join(",").replace("[,", "[").replace(",]", "]") + "]";
                    input->setText(args);
                    input->setToolTip("OSC arguments as a JSON array");
                    line->addWidget(input);
                    auto *apply = button(line, "Send");
                    const auto submit = [this, path, tags, input] { sendControl(path, tags, input->text()); };
                    connect(apply, &QPushButton::clicked, this, submit);
                    connect(input, &QLineEdit::returnPressed, this, submit);
                }
            }
            if (rights & 1) {
                auto *feedback = new QLabel(value.isEmpty() ? "—" : bench::json(value).left(80));
                feedback->setTextInteractionFlags(Qt::TextSelectableByMouse);
                line->addWidget(feedback, 1);
                m_controlValues.insert(path, feedback);
                auto *listen = button(line, "Listen");
                connect(listen, &QPushButton::clicked, this, [this, path] { command("LISTEN", path); });
            }
            rows->addLayout(line);
        }
        for (int i = 0; i < item->childCount() && controls < 200; ++i) add(item->child(i));
    };
    add(selected);
    if (!controls) rows->addWidget(new QLabel("No parameters, actions or signals in this branch."));
    if (controls == 200) rows->addWidget(new QLabel("Select a branch to see more nodes."));
    m_controls->setWidget(panel);
}

void BenchWindow::showFeedback(const QString &path, const QString &value)
{
    if (auto *label = m_controlValues.value(path)) {
        label->setText(value.left(80));
        label->setToolTip(value);
    }
}

void BenchWindow::sendControl(const QString &path, const QString &tags, const QString &arguments)
{
    QString error;
    const auto wire = bench::packet(path, tags, arguments, error);
    if (!error.isEmpty()) { trace("SEND", error); return; }
    sendWire(wire, m_transport->currentText().endsWith("WebSocket"));
}

void BenchWindow::trace(const QString &channel, const QString &message)
{
    m_trace->appendPlainText(QTime::currentTime().toString("HH:mm:ss.zzz") + "  " + channel + "  " + message);
}

void BenchWindow::send()
{
    const auto transport = m_transport->currentText();
    QString error;
    QByteArray wire;
    if (transport.startsWith("Raw")) {
        QString hex = m_arguments->toPlainText();
        hex.remove(QRegularExpression("\\s"));
        if (!QRegularExpression("^(?:[0-9a-fA-F]{2})+$").match(hex).hasMatch()) error = "Enter full bytes as hex.";
        else wire = QByteArray::fromHex(hex.toLatin1());
    } else wire = bench::packet(m_path->text(), m_tags->text(), m_arguments->toPlainText(), error);
    if (!error.isEmpty()) { trace("SEND", error); return; }
    sendWire(wire, transport.endsWith("WebSocket"));
}

void BenchWindow::sendWire(const QByteArray &wire, bool websocket)
{
    if (websocket) {
#ifdef PXS_HAS_WEBSOCKETS
        if (m_ws->state() != QAbstractSocket::ConnectedState) { trace("WS", "Open the WebSocket before sending."); return; }
        m_ws->sendBinaryMessage(wire);
        trace("WS OUT", wire.toHex(' '));
#endif
        return;
    }
    if (m_target.isEmpty()) { trace("UDP", "Connect to an endpoint first."); return; }
    if (m_hostPending) { trace("UDP", "Waiting for the endpoint's HOST_INFO."); return; }
    if (m_host.value("OSC_TRANSPORT").toString("UDP").compare("UDP", Qt::CaseInsensitive) != 0) {
        trace("UDP", "The target advertises a different OSC transport.");
        return;
    }
    if (wire.size() > 65507) { trace("UDP", "The packet exceeds the UDP payload limit."); return; }
    const auto host = m_host.value("OSC_IP").toString(m_target.host());
    const int port = m_host.value("OSC_PORT").toInt(m_target.port(m_target.scheme() == "https" ? 443 : 80));
    if (port < 1 || port > 65535) { trace("UDP", "The advertised OSC port is invalid."); return; }
    const auto generation = m_generation;
    QHostInfo::lookupHost(host, this, [this, wire, port, host, generation](const QHostInfo &info) {
        if (generation != m_generation) return;
        auto addresses = info.addresses();
        if (addresses.isEmpty()) { trace("UDP", info.errorString()); return; }
        auto selected = addresses.front();
        for (const auto &a : addresses) if (a.protocol() == QAbstractSocket::IPv4Protocol) { selected = a; break; }
        if (m_udp.writeDatagram(wire, selected, quint16(port)) < 0) trace("UDP", m_udp.errorString());
        else trace("UDP OUT", QString("%1:%2  %3").arg(host).arg(port).arg(QString::fromLatin1(wire.toHex(' '))));
    });
}

void BenchWindow::command(const QString &name, const QString &path)
{
#ifdef PXS_HAS_WEBSOCKETS
    if (m_ws->state() != QAbstractSocket::ConnectedState) {
        trace("WS", "Open the WebSocket before subscribing.");
        return;
    }
    const QString text = QString::fromUtf8(QJsonDocument(QJsonObject{{"COMMAND", name}, {"DATA", path}}).toJson(QJsonDocument::Compact));
    m_ws->sendTextMessage(text);
    trace("WS OUT", text);
#else
    Q_UNUSED(name)
    Q_UNUSED(path)
    trace("WS", "Build OSCQbench with Qt WebSockets to use LISTEN and IGNORE.");
#endif
}

void BenchWindow::openWebSocket()
{
#ifdef PXS_HAS_WEBSOCKETS
    if (m_target.isEmpty() || m_hostPending || m_ws->state() != QAbstractSocket::UnconnectedState) return;
    QUrl url = m_target;
    url.setScheme(m_target.scheme() == "https" ? "wss" : "ws");
    url.setHost(m_host.value("WS_IP").toString(m_target.host()));
    url.setPort(m_host.value("WS_PORT").toInt(m_target.port(m_target.scheme() == "https" ? 443 : 80)));
    url.setPath("/");
    url.setQuery(QString());
    m_ws->open(url);
#else
    trace("WS", "Build OSCQbench with Qt WebSockets to open this transport.");
#endif
}
