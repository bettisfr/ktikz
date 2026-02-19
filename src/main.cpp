#include <QApplication>
#include <QIcon>

#include "appconfig.h"
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("Francesco Betti Sorbelli");
    app.setApplicationName(QString::fromLatin1(appconfig::APP_NAME));
    const QIcon app_icon = QIcon::fromTheme(QString::fromLatin1(appconfig::APP_ICON_NAME));
    app.setWindowIcon(app_icon);
    mainwindow window;
    window.setWindowIcon(app_icon);
    window.showMaximized();
    return app.exec();
}
