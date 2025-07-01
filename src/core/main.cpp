#include <QApplication>

#include "QtMessageHandler.h"	    // initLogging()
#include "CADventory.h"


int main(int argc, char **argv) 
{
    initLogging();

    QApplication qt(argc, argv);

    CADventory app(argc, argv);
    app.showSplash();
    app.run();

    return qt.exec();
}
