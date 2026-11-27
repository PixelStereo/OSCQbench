#include "Device.h"
#include "BenchWindow.h"
#include "Protocol.h"

#include <pxs/OscService.h>
#include <pxs/core/osc/codec.h>
#include <pxs/Theme.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollArea>
#include <QTreeWidget>
#include <QTimer>

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <memory>

using namespace pxs::core;
using namespace pxs::core::tree;

namespace {

// --check: the device starts, its tree is as every pxs tree must be, and its OSCQuery server answers with every top-level node. For the package
// to be tried before it is published
bool testPackets(Device &device)
{
    struct Case { const char *path, *tag, *args, *hex; };
    static const Case cases[] = {
        {"/a", "", "[]", "2f6100002c000000"},
        {"/N", "N", "[null]", "2f4e00002c4e0000"},
        {"/I", "I", "[null]", "2f4900002c490000"},
        {"/i", "i", "[-1]", "2f6900002c690000ffffffff"},
        {"/h", "h", R"(["1099511627776"])", "2f6800002c6800000000010000000000"},
        {"/f", "f", "[0.5]", "2f6600002c6600003f000000"},
        {"/d", "d", "[3.141592653589793]", "2f6400002c640000400921fb54442d18"},
        {"/s", "s", R"(["abc"])", "2f7300002c73000061626300"},
        {"/S", "S", R"(["sym"])", "2f5300002c53000073796d00"},
        {"/b", "b", R"(["010203"])", "2f6200002c6200000000000301020300"},
        {"/c", "c", R"(["A"])", "2f6300002c63000000000041"},
        {"/m", "m", R"(["0x00903c7f"])", "2f6d00002c6d000000903c7f"},
        {"/t", "t", R"(["0x0123456789abcdef"])", "2f7400002c7400000123456789abcdef"},
        {"/T", "T", "[true]", "2f5400002c540000"},
        {"/F", "F", "[false]", "2f4600002c460000"},
        {"/r", "r", "[\"#FF8040C0\"]", "2f7200002c720000ff8040c0"},
        {"/a", "i[ff]i", "[1,[0.25,0.75],2]", "2f6100002c695b66665d6900000000013e8000003f40000000000002"},
    };
    QString error;
    for (const auto &c : cases) {
        if (bench::packet(c.path, c.tag, c.args, error).toHex() != c.hex) {
            std::fprintf(stderr, "OSCQbench: wrong bytes for %s: %s\n", c.tag, qPrintable(error));
            return false;
        }
    }
    const auto arrayPacket = bench::packet("/types/array", "i[ff]i", "[3,[0.5,0.125],4]", error);
    std::vector<pxs::core::osc::Message> messages;
    if (!pxs::core::osc::decode(reinterpret_cast<const std::uint8_t *>(arrayPacket.data()), std::size_t(arrayPacket.size()), messages)
        || messages.size() != 1 || device.tree().apply(messages.front()) != 1) return false;
    const auto value = device.tree().find("/types/array")->get();
    if (value.size() != 4 || value[0].number() != 3 || value[2].number() != 0.125 || value[3].number() != 4) return false;
    const auto &tree = device.tree();
    for (const auto &mode : {"none", "high", "low", "both"}) {
        const std::string path = std::string("/protocol/clip/") + mode;
        const bool clipLow = path == "/protocol/clip/low" || path == "/protocol/clip/both";
        const bool clipHigh = path == "/protocol/clip/high" || path == "/protocol/clip/both";
        if (tree.apply(path, {Value(-1.0f)}) != 1 || tree.find(path)->get()[0].number() != (clipLow ? 0.0 : -1.0)) return false;
        if (tree.apply(path, {Value(2.0f)}) != 1 || tree.find(path)->get()[0].number() != (clipHigh ? 1.0 : 2.0)) return false;
    }
    device.toggleProbePath();
    if (!tree.find("/protocol/dynamic/value")) return false;
    device.toggleProbePath();
    return !tree.find("/protocol/dynamic/value");
}

int selfTest()
{
    Device device;
    const std::vector<std::string> problems = device.tree().check();
    for (const std::string &problem : problems) std::fprintf(stderr, "OSCQbench: the tree lacks something: %s\n", problem.c_str());
    pxs::OscService service(&device.tree(), QStringLiteral("OSCQbench"));
    service.setLocalOnly(true);
    if (!service.start(0, 0, false)) return 2;
    QNetworkAccessManager net;
    QEventLoop loop;
    QByteArray body;
    QNetworkReply *reply = net.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(service.httpPort()))));
    QObject::connect(reply, &QNetworkReply::finished, [&] {
        if (reply->error() == QNetworkReply::NoError) body = reply->readAll();
        loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    // What the editor writes (its timeline, its transport) is told to the tree: the OSC feedback
    std::vector<std::string> told;
    const int watching = device.tree().addObserver([&told](const pxs::core::tree::Node &node, const ValueList &) { told.push_back(node.path()); });
    Animation edited = device.timeline();
    edited.tracks.front().wave = AnimWave::Saw;
    edited.duration = 2;
    device.setTimeline(edited);
    device.control(AnimAction::Play);
    device.tree().removeObserver(watching);
    const auto saw = [&told](const char *path) { return std::find(told.begin(), told.end(), path) != told.end(); };
    const pxs::core::tree::Node *type = device.tree().find("/generator/wave/type");
    const bool feedback = saw("/generator/wave/type") && saw("/generator/timeline/duration") && saw("/generator/transport/play") && type && type->get &&
                          type->get().front().text() == "saw";
    if (!feedback) std::fprintf(stderr, "OSCQbench: an edit of the timeline was not told to the tree\n");

    const bool packets = testPackets(device);
    if (!packets) std::fprintf(stderr, "OSCQbench: packet or fixture checks failed\n");
    bool served = !body.isEmpty() && feedback && packets;
    for (const std::string &tab : device.sections()) served = served && body.contains(QByteArray("\"") + tab.c_str() + "\"");
    const auto root = QJsonDocument::fromJson(body).object().value("CONTENTS").toObject();
    const auto actions = root.value("actions").toObject().value("CONTENTS").toObject();
    const auto signalNodes = root.value("signals").toObject().value("CONTENTS").toObject();
    const auto rgba = root.value("types").toObject().value("CONTENTS").toObject().value("rgba").toObject();
    const auto nil = actions.value("none").toObject();
    const auto infinitum = actions.value("infinitum").toObject();
    const auto impulse = signalNodes.value("infinitum").toObject();
    served = served && nil.value("TYPE") == QStringLiteral("N") && nil.value("ACCESS").toInt() == 2
             && infinitum.value("TYPE") == QStringLiteral("I") && infinitum.value("ACCESS").toInt() == 2
             && impulse.value("TYPE") == QStringLiteral("I") && impulse.value("ACCESS").toInt() == 1
             && rgba.value("TYPE") == QStringLiteral("r")
             && rgba.value("VALUE").toArray().at(0).toString().compare(QStringLiteral("#FF8040C0"), Qt::CaseInsensitive) == 0;
    std::puts(served && problems.empty() ? "OSCQbench: ok" : "OSCQbench: the tree or the server is not what it must be");
    return served && problems.empty() ? 0 : 1;
}

bool until(const std::function<bool()> &condition)
{
    QEventLoop loop;
    QTimer tick;
    QObject::connect(&tick, &QTimer::timeout, &loop, [&] { if (condition()) loop.quit(); });
    tick.start(20);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    return condition();
}

int checkWindow()
{
    Device device;
    pxs::OscService service(&device.tree(), QStringLiteral("OSCQbench window check"));
    service.setLocalOnly(true);
    if (!service.start(0, 0, false)) return 2;
    BenchWindow window(device, service);
    window.show();
    window.resize(1000, 700);
    auto *tree = window.findChild<QTreeWidget *>("namespace");
    auto *panel = window.findChild<QScrollArea *>("publishedControls");
    auto *status = window.findChild<QLabel *>("endpointStatus");
    if (!tree || !panel || !status || window.size() != QSize(1000, 700)
        || !until([&] { return tree->topLevelItemCount() && panel->widget() && status->text().contains("OSCQbench window check"); })) return 1;
    const auto select = [&](const QString &path) {
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->toolTip(0) != path) continue;
            tree->setCurrentItem(*it);
            return true;
        }
        return false;
    };
    const auto press = [&](const QString &label) {
        for (auto *button : panel->widget()->findChildren<QPushButton *>()) {
            if (button->text() != label) continue;
            button->click();
            return true;
        }
        return false;
    };
    if (!select("/protocol/clip/both")) return 1;
    auto *value = panel->widget()->findChild<QDoubleSpinBox *>();
    if (!value) return 1;
    value->setValue(2);
    if (!press("Send") || !until([&] { return device.tree().find("/protocol/clip/both")->get()[0].number() == 1.0; })) return 1;
    if (!select("/protocol/toggle_path") || !press("Trigger")
        || !until([&] { return device.tree().find("/protocol/dynamic/value"); })) return 1;
    std::puts("OSCQbench window: namespace, resize, parameter write and action passed");
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    bool headlessMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--check") == 0) {
            QCoreApplication core(argc, argv);
            return selfTest();
        }
        if (std::strcmp(argv[i], "--check-window") == 0) {
            QApplication app(argc, argv);
            return checkWindow();
        }
        if (std::strcmp(argv[i], "--no-window") == 0) headlessMode = true;
    }
    std::unique_ptr<QCoreApplication> app;
    if (headlessMode) app = std::make_unique<QCoreApplication>(argc, argv);
    else {
        auto gui = std::make_unique<QApplication>(argc, argv);
        gui->setOrganizationDomain("pixelstereo.fr");
        pxs::apply(*gui, pxs::graphite());
        app = std::move(gui);
    }
    app->setApplicationName("OSCQbench");
    app->setApplicationVersion(QStringLiteral(OSCQBENCH_VERSION));
    app->setOrganizationName("Pixel Stereo");

    QCommandLineParser cli;
    cli.setApplicationDescription("Inspect OSCQuery devices and exercise an OSCQuery fixture.");
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption name({"n", "name"}, "The name it announces on the network.", "name", "OSCQbench");
    QCommandLineOption oscPort({"o", "osc-port"}, "UDP port for OSC (0: any free one).", "port", "9100");
    QCommandLineOption httpPort({"p", "http-port"}, "TCP port for OSCQuery HTTP and WebSocket (0: any free one).", "port", "5679");
    QCommandLineOption local("local", "Reachable from this computer only, and not announced.");
    QCommandLineOption headless("no-window", "No window: only the device.");
    QCommandLineOption check("check", "Start, check the tree and the OSCQuery server, and exit (no window).");
    QCommandLineOption checkWindowOption("check-window", "Check the resizable controls panel and its OSC output.");
    cli.addOptions({name, oscPort, httpPort, local, headless, check, checkWindowOption});
    cli.process(*app);

    Device device;
    for (const std::string &problem : device.tree().check()) qWarning("OSCQbench: the tree lacks something: %s", problem.c_str()); // what every pxs tree must have
    pxs::OscService service(&device.tree(), cli.value(name));
    service.setLocalOnly(cli.isSet(local));
    if (!service.start(cli.value(oscPort).toUShort(), cli.value(httpPort).toUShort(), !cli.isSet(local))) {
        qCritical("OSCQbench: could not open its ports");
        return 1;
    }
    qInfo("OSCQbench: OSCQuery on http://127.0.0.1:%d/, OSC on udp %d", service.httpPort(), service.oscPort());

    if (cli.isSet(headless)) return app->exec();
    BenchWindow window(device, service);
    window.show();
    return app->exec();
}
