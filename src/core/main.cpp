#include "UiShim.h"

#include "QtMessageHandler.h"	    // initLogging()
#include "CADventory.h"


int main(int argc, char **argv) 
{
    initLogging();

    cad_ui::AppT app(argc, argv);

    CADventory cadventory(argc, argv);
    cadventory.run();

    return app.exec();
}
