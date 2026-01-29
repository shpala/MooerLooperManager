#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("MooerLooperManager");
    app.setApplicationName("MooerLooperManager");
    MainWindow w;
    w.show();
    return app.exec();
}
