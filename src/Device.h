#pragma once
// Deterministic OSCQuery fixtures and a generator for streaming tests.
#include <pxs/TimelineHost.h>
#include <pxs/core/tree/tree.h>

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <random>
#include <string>
#include <vector>

// The generator is a timeline of one track, an oscillator (or keys), the same as NyxMap's (pxs-core's logic, pxs-qt's editor). Its
// settings are parameters of /generator, and the editor of the window edits the same timeline: both say every change to the tree.
class Device : public QObject, public TimelineHost
{
public:
    explicit Device(QObject *parent = nullptr);

    // The generator's timeline, as the editor reads it and writes it back (it keeps where it is: its state, its clock)
    const Animation &timeline() const { return m_timeline; }
    void setTimeline(const Animation &edited);
    // Its transport (the editor's buttons, the parameters of /generator/transport)
    void control(AnimAction action, double time = 0);
    AnimationPlayback playback() const;

    // TimelineHost: what the track drives, the generator's output
    std::vector<ParamInfo> animatableParams(quint64 holder) const override;
    bool animParamValue(quint64 holder, const QString &path, double *value) const override;

    pxs::core::tree::Tree &tree() { return m_tree; }
    const std::vector<std::string> &sections() const { return m_sections; }
    void toggleProbePath();
    void emitProbeSignal();

private:
    // What the generator is: every parameter of /generator reads and writes one of these
    struct State {
        bool flash = false;
        std::string label = "OSCQbench";
        double value = 0; // what it says now
        int passesDone = 0;
        bool above = false;
        float x = 0.5f, y = 0.5f, r = 1.0f, g = 0.5f, b = 0.0f;
        bool tint = true;
        int uptime = 0, counter = 0;
    };

    void buildGenerator();
    void buildValues();
    void buildTypes();
    void buildActions();
    void buildSignals();
    void buildAccess();
    void buildStructure();
    void buildProtocol();

    // A container, said: its label and what it is for
    pxs::core::tree::Node &group(const char *path, const char *label, const char *description = "");
    template <class T, class Configure>
    pxs::core::tree::Node &bound(const char *path, const char *label, pxs::core::tree::Parameter::Type type, T &ref, Configure configure);
    template <class Configure>
    pxs::core::tree::Node &stored(const char *path, const char *label, pxs::core::tree::Parameter::Type type, pxs::core::Value initial, Configure configure);
    pxs::core::tree::Node &raw(const char *path, const char *type, const char *label, const char *description, std::vector<pxs::core::tree::ArgRange> ranges,
                               pxs::core::ValueList initial, std::vector<std::string> extendedType = {});
    pxs::core::tree::Node &signal(const char *path, const char *type, const char *label, const char *description, std::vector<pxs::core::tree::ArgRange> ranges = {},
                                  std::vector<std::string> extendedType = {});
    void action(const char *path, const char *type, const char *label, const char *description, std::function<bool(const pxs::core::ValueList &)> handler);
    // A parameter and the nodes under it that are views of it: writing one tells the others
    void relate(const std::string &parent, const std::vector<std::string> &children);
    void said(const std::string &path, const pxs::core::ValueList &args); // the last action, on /generator/signals/last_action

    // What the parameters of /generator say, to tell the tree which changed
    struct Said {
        bool play = false, keys = false;
        int wave = 0, loop = 0, repeat = 0, passesDone = 0;
        double frequency = 0, amplitude = 0, center = 0, phase = 0, duration = 0, speed = 0;
    };
    Said said() const;
    void tell(const Said &before); // the tree told of what differs from `before`
    AnimTrack &track() { return m_timeline.tracks.front(); }
    const AnimTrack &track() const { return m_timeline.tracks.front(); }
    int passesDone() const;
    void tick();

    pxs::core::tree::Tree m_tree;
    std::vector<std::string> m_sections;
    float m_probePulse = 0;
    State s;
    Animation m_timeline;
    QTimer m_timer;
    QElapsedTimer m_clock;
    std::mt19937 m_rng{std::random_device{}()};
    double m_up = 0; // seconds since it started
};
