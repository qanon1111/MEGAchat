#ifndef CHATWINDOW_H
#define CHATWINDOW_H
#include "ui_chatWindow.h"
#include "QTMegaChatRoomListener.h"
#include "megachatapi.h"
#include "chatItemWidget.h"
#include "chatMessage.h"
#include "megaLoggerApplication.h"
#include "MainWindow.h"
#include <QMessageBox>

#ifndef KARERE_DISABLE_WEBRTC
#include "callGui.h"
/*
namespace rtcmodule
{
    class ICallHandler{};
}
class CallGui: public rtcModule::ICallHandler {};*/
#endif


#define NMESSAGES_LOAD 16   // number of messages to load at every fetch
class ChatMessage;
class MainWindow;

namespace Ui{
class ChatWindowUi;
}
class ChatItemWidget;

class ChatWindow : public QDialog, megachat::MegaChatRoomListener
{
    Q_OBJECT
    public:
        ChatWindow(QWidget *parent, megachat::MegaChatApi *mMegaChatApi, megachat::MegaChatRoom *cRoom, const char *title);
        virtual ~ChatWindow();
        void openChatRoom();
        void onChatRoomUpdate(megachat::MegaChatApi *api, megachat::MegaChatRoom *chat);
        void onMessageReceived(megachat::MegaChatApi* api, megachat::MegaChatMessage *msg);
        void onMessageUpdate(megachat::MegaChatApi* api, megachat::MegaChatMessage *msg);
        void onMessageLoaded(megachat::MegaChatApi* api, megachat::MegaChatMessage *msg);
        void onHistoryReloaded(megachat::MegaChatApi* api, megachat::MegaChatRoom *chat);
        void deleteChatMessage(megachat::MegaChatMessage *msg);
        void createMembersMenu(QMenu& menu);
        void truncateChatUI();
        void connectCall();
        void hangCall();
        void setChatTittle(const char *title);
        bool eraseChatMessage(megachat::MegaChatMessage *msg, bool temporal);
        void moveManualSendingToSending(megachat::MegaChatMessage *msg);
        void updateMessageFirstname(megachat::MegaChatHandle contactHandle, const char *firstname);
        void setMessageHeight(megachat::MegaChatMessage *msg, QListWidgetItem *item);
        QListWidgetItem *addMsgWidget (megachat::MegaChatMessage *msg, int index);
        ChatMessage *findChatMessage(megachat::MegaChatHandle msgId);
        megachat::MegaChatHandle getMessageId(megachat::MegaChatMessage *msg);
#ifndef KARERE_DISABLE_WEBRTC
        CallGui *getCallGui() const;
        void setCallGui(CallGui *callGui);
#endif
    protected:
        Ui::ChatWindowUi *ui;
#ifndef KARERE_DISABLE_WEBRTC
        CallGui *mCallGui;
#endif
        MainWindow *mMainWin;
        megachat::MegaChatApi *mMegaChatApi;
        mega::MegaApi *mMegaApi;
        megachat::MegaChatRoom *mChatRoom;
        ChatItemWidget *mChatItemWidget;
        MegaLoggerApplication *mLogger;
        megachat::QTMegaChatRoomListener *megaChatRoomListenerDelegate;
        std::map<megachat::MegaChatHandle, ChatMessage *> mMsgsWidgetsMap;
        std::string mChatTitle;
        int nSending;
        int loadedMessages;
        int nManualSending;
        int mPendingLoad;

    private slots:
        void onMsgListRequestHistory();
        void onMemberSetPriv();
        void onMemberRemove();
        void onMsgSendBtn();
        void onMemberAdd();
        void onTruncateChat();
        void onMembersBtn(bool);
#ifndef KARERE_DISABLE_WEBRTC
        void onCallBtn(bool video);
#endif
    protected slots:
#ifndef KARERE_DISABLE_WEBRTC
        void closeEvent(QCloseEvent *event);
        void createCallGui(bool);
        void onVideoCallBtn(bool);
        void onAudioCallBtn(bool);
        void deleteCallGui();
#endif

    friend class CallGui;
    friend class ChatMessage;
    friend class CallAnswerGui;
    friend class MainWindow;
};
#endif // CHATWINDOW_H
