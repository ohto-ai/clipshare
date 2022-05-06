#include "ClipShareWindow.h"
#include "SingleApplication.h"
#include <cpp-httplib/httplib.h>
#include <ghc/filesystem.hpp>
#include <fplus/fplus.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

int main(int argc, char *argv[])
{
    SingleApplication a(argc, argv);

    spdlog::info("CLIPSHARE initializing~");
    if (a.instanceRunning())
    {
        spdlog::info("Another application has running, bye~");
        return 0;
    }

    a.setWindowIcon(QIcon{ "res/icon/main.png" });

    ClipShareWindow w;
    // w.show();

	spdlog::info("Interface crate.");
    return a.exec();
}
