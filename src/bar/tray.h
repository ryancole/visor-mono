#pragma once

class App;

// Notification-area icon with visor's settings menu, built directly on
// Shell_NotifyIcon / TrackPopupMenu so it doesn't pull in Qt Widgets.
//
// Left or right click opens the menu:
//   Renderer > CPU / GPU      (saved; restarts visor to apply)
//   Reload config
//   Open config folder
//   Quit
class TrayIcon
{
public:
    explicit TrayIcon(App *app);
    ~TrayIcon();

    TrayIcon(const TrayIcon &) = delete;
    TrayIcon &operator=(const TrayIcon &) = delete;

    // Window procedure hook; returns true if the message was handled.
    bool handleMessage(unsigned msg, unsigned long long wParam, long long lParam);

private:
    void add();
    void showMenu(int x, int y);
    void runCommand(unsigned id);

    App *m_app;
    void *m_hwnd = nullptr;
    void *m_icon = nullptr;
};
