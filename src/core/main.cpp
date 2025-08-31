#include "UiShim.h"

#include "Logger.h"	    // initLogging()
#include "CADventory.h"


int main(int argc, char **argv) 
{
    initLogging();

    cad_ui::AppT app(argc, argv);

    CADventory cadventory(argc, argv);
    cadventory.run();

    return app.exec();
}
