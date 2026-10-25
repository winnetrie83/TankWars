#include "mainwindow.h"
#include <QApplication>
#include <QCoreApplication>
#include <QStyleFactory>


int main(int argc,char **argv){
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName("Winnetrie");
    QCoreApplication::setApplicationName("TankWars");
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    tw::MainWindow window;window.show();
    return app.exec();
}
