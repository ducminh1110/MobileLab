#pragma once
// What the editor area shows. Navigators, the jump bar history and Open Quickly all speak Location.
#include <QMetaType>
#include <QString>

struct Location {
    enum Kind { Welcome, Target, Run, RunTarget, Container, Settings };
    Kind kind = Welcome;
    QString id;      // Target: AVD name, Run/RunTarget: run id
    QString sub;     // RunTarget: AVD name
    QString tab;     // Run/RunTarget: summary | tests | logs
    int line = -1;   // log line to reveal (zero based, in the log the tab shows)
    bool operator==(const Location &o) const { return kind == o.kind && id == o.id && sub == o.sub && tab == o.tab && line == o.line; }
    bool operator!=(const Location &o) const { return !(*this == o); }
    bool sameThing(const Location &o) const { return kind == o.kind && id == o.id && sub == o.sub; }
};
Q_DECLARE_METATYPE(Location)
