#include "ClipShareWindow.h"
#include "SingleApplication.h"
#include <cpp-httplib/httplib.h>
#include <ghc/filesystem.hpp>
#include <fplus/fplus.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <QTextCodec>

int main(int argc, char *argv[])
{
    SingleApplication a(argc, argv);
    a.setApplicationName("clipshare");
    a.setDesktopFileName("clipshare");
    a.setApplicationVersion("0.0.1.dev");
    a.setOrganizationName("ohtoai");
    a.setOrganizationDomain("ohtoai.top");
    a.setWindowIcon(QIcon{ ":/ClipShareWindow/res/icon/main.png" });

    if (a.instanceRunning())
    {
        spdlog::warn("[Application] Another application has running, bye~");
        return 0;
    }

    ClipShareWindow w;
    spdlog::info("[Application] CLIPSHARE initializing~");
    w.show();

	spdlog::info("[Application] Interface crate.");
    return a.exec();
}
