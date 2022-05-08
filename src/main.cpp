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

    // spdlog::set_level(spdlog::level::trace);

    spdlog::info("[Application] CLIPSHARE initializing~");
    if (a.instanceRunning())
    {
        spdlog::warn("[Application] Another application has running, bye~");
        return 0;
    }

    a.setWindowIcon(QIcon{ ":/ClipShareWindow/res/icon/main.png" });

    ClipShareWindow w;
    // w.show();

	spdlog::info("[Application] Interface crate.");
    return a.exec();
}
