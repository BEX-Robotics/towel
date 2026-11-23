#include "mainwindow.h"
#include "repo.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QStyleFactory>
#include <QTimer>
#include <git2.h>

namespace {
// The path given on the command line; else the repository we were started in;
// else the last one opened that still exists.
QString startRepo(const QStringList &args) {
    for (int i = 1; i < args.size(); ++i)
        if (!args[i].startsWith(QLatin1Char('-'))) return QDir(args[i]).absolutePath();

    git_buf found = GIT_BUF_INIT;
    const bool inRepo = git_repository_discover(&found, QDir::currentPath().toUtf8().constData(), 0, 0) == 0;
    git_buf_dispose(&found);
    if (inRepo) return QDir::currentPath();

    for (const QString &r : QSettings().value("recentRepos").toStringList())
        if (QDir(r).exists()) return r;

    return QDir::currentPath();
}
} // namespace

int main(int argc, char *argv[]) {
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

    // Dev runs (screenshots, demo scripts) get a throwaway settings folder, so a test never
    // changes the user's window size, zoom, recent repositories or message drafts.
    if (qEnvironmentVariableIsSet("TOWEL_SHOT") || qEnvironmentVariableIsSet("TOWEL_DEMO_SCRIPT")) {
        // INI, because on macOS and Windows the native format ignores setPath
        QSettings::setDefaultFormat(QSettings::IniFormat);
        const QString dir = QDir::tempPath() + QStringLiteral("/towel-dev-settings");
        QDir(dir).removeRecursively(); // each dev run starts from defaults, like a fresh install
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
    }

    const QString start = startRepo(app.arguments());

    QApplication::setFont(theme::uiFont(13)); // the same app font at 100% as after any zoom change
    theme::setZoom(QSettings().value("zoom", 1.0).toDouble());
    MainWindow w;
    w.show();
    Repo::instance()->open(start);
    w.runDemo();

    // Dev aid: TOWEL_SHOT=/path.png [TOWEL_SHOT_DELAY=ms] saves a window grab and quits.
    if (const QString shot = qEnvironmentVariable("TOWEL_SHOT"); !shot.isEmpty()) {
        const int delay = qEnvironmentVariableIntValue("TOWEL_SHOT_DELAY");
        QTimer::singleShot(delay > 0 ? delay : 4000, &app, [&w, shot] {
            w.grab().save(shot);
            QCoreApplication::quit();
        });
    }
    const int rc = app.exec();
    git_libgit2_shutdown();
    return rc;
}
