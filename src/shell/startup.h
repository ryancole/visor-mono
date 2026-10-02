#pragma once

#include <QObject>

namespace visor {

// The programs Explorer starts at sign-in, which never run without it:
// RunOnce, the Run keys and the Startup folders, each subject to the
// switches in Settings > Apps > Startup (the StartupApproved keys). Replace
// mode only, once per sign-in: Explorer coming back later (Ctrl+Alt+Q)
// runs them again on its own, which is its business.
class Startup : public QObject
{
    Q_OBJECT

public:
    explicit Startup(QObject *parent = nullptr);

    // Runs everything that is due, unless this sign-in already had it.
    // Launches are detached (common/launch.h); nothing waits for them.
    void run();

private:
    void runRegistry(const QString &key, bool once);
    void runFolder(const QString &folder);
};

} // namespace visor
