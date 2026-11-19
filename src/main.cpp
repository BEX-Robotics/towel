#include "mainwindow.h"
#include "repo.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QStyleFactory>
#include <QTimer>
#include <git2.h>

int main(int argc, char *argv[])
{
    // On a KDE session Qt would load the Plasma platform theme: ~750 extra
    // libraries (KIO, Qt Quick, QML, OpenGL, Mesa/LLVM) only to style dialogs
    // and icons. Towel paints its own theme, so hide the desktop from Qt while
    // it starts up, then restore the environment for git and child processes.
    const char *vars[] = {"XDG_CURRENT_DESKTOP", "KDE_FULL_SESSION", "KDE_SESSION_VERSION", "DESKTOP_SESSION", "GNOME_DESKTOP_SESSION_ID"};
    QList<QPair<const char *, QByteArray>> saved;
    const bool lean = qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME");
    if (lean) {
        for (const char *v : vars) {
            if (qEnvironmentVariableIsSet(v)) saved.append({v, qgetenv(v)});
            qunsetenv(v);
        }
    }
    QApplication app(argc, argv);
    for (const auto &[k, v] : saved) qputenv(k, v);
    app.setApplicationName(QStringLiteral("towel"));
    app.setApplicationDisplayName(QStringLiteral("Towel"));
    app.setOrganizationName(QStringLiteral("towel"));
    app.setDesktopFileName(QStringLiteral("towel"));

    // Our own dark look, independent of the desktop theme.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette pal;
    pal.setColor(QPalette::Window, theme::bg);
    pal.setColor(QPalette::WindowText, theme::text);
    pal.setColor(QPalette::Base, theme::field);
    pal.setColor(QPalette::AlternateBase, theme::panel);
    pal.setColor(QPalette::Text, theme::text);
    pal.setColor(QPalette::Button, theme::panel);
    pal.setColor(QPalette::ButtonText, theme::text);
    pal.setColor(QPalette::Highlight, theme::selection);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, QColor(0x0f, 0x11, 0x14));
    pal.setColor(QPalette::ToolTipText, theme::text);
    pal.setColor(QPalette::PlaceholderText, theme::faint);
    app.setPalette(pal);
    app.setStyleSheet(theme::styleSheet());

    git_libgit2_init();
    // The object cache would keep every commit of the history walk alive (up to 256 MB)
    // and fragment the heap; we read each object once, so it buys nothing.
    git_libgit2_opts(GIT_OPT_ENABLE_CACHING, 0);

    QString start = QDir::currentPath();
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i)
        if (!args[i].startsWith(QLatin1Char('-'))) start = QDir(args[i]).absolutePath();

    MainWindow w;
    w.show();
    Repo::instance()->open(start);
    w.runDemo();

    // Dev aid: TOWEL_SHOT=/path.png [TOWEL_SHOT_DELAY=ms] saves a window grab and quits.
    if (const QString shot = qEnvironmentVariable("TOWEL_SHOT"); !shot.isEmpty()) {
        QTimer::singleShot(qEnvironmentVariableIntValue("TOWEL_SHOT_DELAY") ?: 4000, &app, [&w, shot] {
            w.grab().save(shot);
            QCoreApplication::quit();
        });
    }
    const int rc = app.exec();
    git_libgit2_shutdown();
    return rc;
}
