#include "editor.h"
#include "widgets.h"
#include <QApplication>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("AfterimageStudio");
    app.setApplicationName("Afterimage");
    app.setApplicationDisplayName("余像 Afterimage");
    app.setApplicationVersion("1.3.0");
    QTranslator zh;
    if (zh.load("qtbase_zh_CN", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&zh);
    applyTheme();
    Editor window;
    window.show();
    const auto args = app.arguments();
    if (args.size() > 1 && QFileInfo(args[1]).isFile())
        window.openPath(args[1]);
    return app.exec();
}
