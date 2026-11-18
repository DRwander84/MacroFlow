#include <QApplication>
#include <QFile>
#include <QIcon>

#include "Application.h"
#include "ui/ThemedDialog.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
    // Force Per-Monitor DPI Awareness V2 before anything else so hook coordinates and cursor
    // placement are both exact physical virtual-desktop pixels (no DPI virtualization).
#ifdef _WIN32
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    QApplication app(argc, argv);
    app.setApplicationName("MacroFlow");
    app.setOrganizationName("MacroFlow");
    app.setApplicationVersion("1.0.0");
    app.setWindowIcon(QIcon(":/icons/logo.png"));  // taskbar, dialogs, Alt+Tab: every window shows the logo

    QFile qss(":/styles/dark.qss");
    if (qss.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));

#ifdef _WIN32
    // Single instance: two instances would fight over the same global hotkeys.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\MacroFlow.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        mmr::dialogs::message(nullptr, "MacroFlow", "The application is already running.");
        return 0;
    }
#endif

    app.setQuitOnLastWindowClosed(true);

    mmr::Application core;
    QString error;
    if (!core.init(&error)) {
        mmr::dialogs::message(nullptr, "Startup failed", error, mmr::dialogs::Kind::Error);
        return 1;
    }
    core.show();
    const int rc = app.exec();
#ifdef _WIN32
    if (mutex) CloseHandle(mutex);
#endif
    return rc;
}
