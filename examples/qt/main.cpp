#ifdef _WIN32
    #include <winsock2.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <QApplication>
#include <QDir>
#include "mainwindow.h"
#include "chatWindow.h"
#include <base/gcm.h>
#include <base/services.h>
#include <chatClient.h>
#include <sdkApi.h>
#include <chatd.h>
#include <mega/megaclient.h>
#include <karereCommon.h>
#include <fstream>

using namespace std;
using namespace promise;
using namespace mega;
using namespace karere;

MainWindow* mainWin = NULL;

struct GcmEvent: public QEvent
{
    static const QEvent::Type type;
    void* ptr;
    GcmEvent(void* aPtr): QEvent(type), ptr(aPtr){}
};
const QEvent::Type GcmEvent::type = (QEvent::Type)QEvent::registerEventType();

class AppDelegate: public QObject
{
    Q_OBJECT
public slots:
    void onAppTerminate();
public:
    virtual bool event(QEvent* event)
    {
        if (event->type() != GcmEvent::type)
            return false;

        megaProcessMessage(static_cast<GcmEvent*>(event)->ptr);
        return true;
    }
};

AppDelegate appDelegate;

extern "C" void myMegaPostMessageToGui(void* msg)
{
    QEvent* event = new GcmEvent(msg);
    QApplication::postEvent(&appDelegate, event);
}

using namespace strophe;

void setVidencParams();

void sigintHandler(int)
{
    printf("SIGINT Received\n");
    fflush(stdout);
    marshallCall([]{appDelegate.onAppTerminate();});

}

std::string gAppDir = karere::createAppDir();
std::unique_ptr<karere::Client> gClient;
std::unique_ptr<::mega::MegaApi> gSdk;
int main(int argc, char **argv)
{
    karere::globalInit(myMegaPostMessageToGui, 0, (gAppDir+"/log.txt").c_str(), 500);
//    ::mega::MegaClient::APIURL = "https://staging.api.mega.co.nz/";
//    gLogger.addUserLogger("karere-remote", new RemoteLogger);

#ifdef __APPLE__
//Set qt plugin dir for release builds
#ifdef NDEBUG
    QDir dir(argv[0]);
    #ifdef __APPLE__
        dir.cdUp();
        dir.cdUp();
        dir.cd("Plugins");
    #else
        dir.cdUp();
        dir.cd("QtPlugins");
    #endif
    QApplication::setLibraryPaths(QStringList(dir.absolutePath()));
#endif
#endif
    QApplication a(argc, argv);
    a.setQuitOnLastWindowClosed(false);

    mainWin = new MainWindow();
    gSdk.reset(new ::mega::MegaApi("karere-native", gAppDir.c_str(), "Karere Native"));
    gClient.reset(new karere::Client(*gSdk, *mainWin, gAppDir, 0));
    mainWin->setClient(*gClient);
    QObject::connect(qApp, SIGNAL(lastWindowClosed()), &appDelegate, SLOT(onAppTerminate()));
    char buf[256];
    const char* sid = nullptr;
    std::ifstream sidf(gAppDir+"/sid");
    if (!sidf.fail())
    {
        sidf.getline(buf, 256);
        if (!sidf.fail())
            sid = buf;
    }
    sidf.close();
    gClient->loginSdkAndInit(sid)
    .then([sid]()
    {
        if (!sid)
        {
            KR_LOG_DEBUG("Client initialized with new session");
            std::ofstream osidf(gAppDir+"/sid");
            const char* sdkSid = gSdk->dumpSession();
            assert(sdkSid);
            osidf << sdkSid;
            osidf.close();
        }
        else
        {
            KR_LOG_DEBUG("Client initialized");
        }
    })
    .then([]()
    {
        setVidencParams();
    })
    .fail([](const promise::Error& error)
    {
        QMessageBox::critical(mainWin, "rtctestapp", QString::fromLatin1("Client startup failed with error:\n")+QString::fromStdString(error.msg()));
//        mainWin->close();
//        exit(1);
    });

    gClient->connect(Presence::kInvalid);
    signal(SIGINT, sigintHandler);
    return a.exec();
}
void setVidencParams()
{
#ifndef KARERE_DISABLE_WEBRTC
    const char* val;
    auto& rtc = *gClient->rtc;
    if ((val = getenv("KR_VIDENC_MAXH")))
    {
        rtc.setMediaConstraint("maxHeight", val);
    }
    if ((val = getenv("KR_VIDENC_MAXW")))
    {
        rtc.setMediaConstraint("maxWidth", val);
    }

    if ((val = getenv("KR_VIDENC_MAXBR")))
    {
        rtc.vidEncParams.maxBitrate = atoi(val);
    }
    if ((val = getenv("KR_VIDENC_MINBR")))
    {
        rtc.vidEncParams.minBitrate = atoi(val);
    }
    if ((val = getenv("KR_VIDENC_MAXQNT")))
    {
        rtc.vidEncParams.maxQuant = atoi(val);
    }
    if ((val = getenv("KR_VIDENC_BUFLAT")))
    {
        rtc.vidEncParams.bufLatency = atoi(val);
    }
#endif
}

void AppDelegate::onAppTerminate()
{
    gClient->terminate()
    .then([this]()
    {
        return gSdk->localLogout(nullptr);
    })
    .fail([](const promise::Error& err)
    {
        KR_LOG_ERROR("Error logging out the Mega client: ", err.what());
    })
    .then([this]()
    {

        marshallCall([]() //post destruction asynchronously so that all pending messages get processed before that
        {
            qApp->quit(); //stop processing marshalled call messages
            gClient.reset();
            karere::globalCleanup();
        });
    });
}
#include <main.moc>
