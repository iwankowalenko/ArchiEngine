#include "Application.h"

int main()
{
    archi::Application app;
    if (!app.Init())
        return -1;
    return app.Run();
}