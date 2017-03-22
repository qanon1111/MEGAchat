#ifndef CHATD_DB_H
#define CHATD_DB_H

#include "db.h"
#include "chatd.h"
extern sqlite3* db;

class ChatdSqliteDb: public chatd::DbInterface
{
protected:
    sqlite3* mDb;
    chatd::Chat& mMessages;
    std::string mSendingTblName;
    std::string mHistTblName;
public:
    ChatdSqliteDb(chatd::Chat& msgs, sqlite3* db, const std::string& sendingTblName="sending", const std::string& histTblName="history")
        :mDb(db), mMessages(msgs), mSendingTblName(sendingTblName), mHistTblName(histTblName){}
    virtual void getHistoryInfo(chatd::ChatDbInfo& info)
    {
        SqliteStmt stmt(mDb, "select min(idx), max(idx) from history where chatid=?1");
        stmt.bind(mMessages.chatId()).step(); //will always return a row, even if table empty
        auto minIdx = stmt.intCol(0); //WARNING: the chatd implementation uses uint32_t values for idx.
        info.newestDbIdx = stmt.intCol(1);
        if (sqlite3_column_type(stmt, 0) == SQLITE_NULL) //no db history
        {
            memset(&info, 0, sizeof(info)); //actually need to zero only oldestDbId
            return;
        }
        SqliteStmt stmt2(mDb, "select msgid from "+mHistTblName+" where chatid=?1 and idx=?2");
        stmt2 << mMessages.chatId() << minIdx;
        stmt2.stepMustHaveData();
        info.oldestDbId = stmt2.uint64Col(0);
        stmt2.reset().bind(2, info.newestDbIdx);
        stmt2.stepMustHaveData();
        info.newestDbId = stmt2.uint64Col(0);
        if (!info.newestDbId)
        {
            CHATD_LOG_WARNING("Db: Newest msgid in db is null, telling chatd we don't have local history");
            info.oldestDbId = 0;
        }
        SqliteStmt stmt3(mDb, "select last_seen, last_recv from chats where chatid=?");
        stmt3 << mMessages.chatId();
        stmt3.stepMustHaveData();
        info.lastSeenId = stmt3.uint64Col(0);
        info.lastRecvId = stmt3.uint64Col(1);
    }
    void assertAffectedRowCount(int count, const char* opname=nullptr)
    {
        auto actual = sqlite3_changes(mDb);
        if (actual == count)
            return;
        std::string msg;
        if (opname)
            msg.append(opname).append(": ");
        msg.append(": unexpected number of rows affected: expected ")
           .append(std::to_string(count)).append(", actual ")
           .append(std::to_string(actual));
        throw std::runtime_error(msg);
    }
    void commit()
    {
        sqliteSimpleQuery(mDb, "COMMIT TRANSACTION");
        sqliteSimpleQuery(mDb, "BEGIN TRANSACTION");
    }
    void saveMsgToSending(chatd::Chat::SendingItem& item)
    {
        assert(item.msg);
        assert(item.isMessage());
        auto msg = item.msg;
        Buffer rcpts;
        item.recipients.save(rcpts);
        sqliteQuery(mDb, "insert into sending (chatid, opcode, ts, msgid, msg, type, updated, "
                         "recipients, backrefid, backrefs) values(?,?,?,?,?,?,?,?,?,?)",
            (uint64_t)mMessages.chatId(), item.opcode(), (int)time(NULL), msg->id(),
            *msg, msg->type, msg->updated, rcpts, msg->backRefId, msg->backrefBuf());
        item.rowid = sqlite3_last_insert_rowid(mDb);
        commit();
    }
    virtual void updateMsgInSending(const chatd::Chat::SendingItem& item)
    {
        assert(item.msg);
        sqliteQuery(mDb, "update sending set msg = ?, updated = ? where rowid = ?",
            *item.msg, item.msg->updated, item.rowid);
        assertAffectedRowCount(1, "updateMsgInSending");
        commit();
    }
    virtual void confirmKeyOfSendingItem(uint64_t rowid, chatd::KeyId keyid)
    {
        sqliteQuery(mDb, "update sending set keyid = ? where rowid = ?",
                    keyid, rowid);
        assertAffectedRowCount(1, "confirmKeyOfSendingItem");
        commit();
    }
    virtual void addBlobsToSendingItem(uint64_t rowid,
                    const chatd::MsgCommand* msgCmd, const chatd::Command* keyCmd)
    {
        //WARNING: Must cast *msgCmd and *keyCmd to StaticBuffer, otherwise
        //compiler (at least clang on MacOS) seems not able to properly determine
        //the argument type for the template parameter to sqlQuery(), which
        //compiles without any warning, but results is corrupt data written to the db!
        sqliteQuery(mDb, "update sending set msg_cmd=?, key_cmd=? where rowid=?",
            msgCmd?static_cast<StaticBuffer>(*msgCmd):StaticBuffer(nullptr, 0),
            keyCmd?static_cast<StaticBuffer>(*keyCmd):StaticBuffer(nullptr, 0), rowid);
        assertAffectedRowCount(1,"addCommandBlobToSendingItem");
        commit();
    }
    virtual void sendingItemMsgupdxToMsgupd(const chatd::Chat::SendingItem& item, karere::Id msgid)
    {
        assert(item.opcode() == chatd::OP_MSGUPDX);
        sqliteQuery(mDb,
            "update sending set opcode=?, msgid=? where chatid=? and rowid=? and opcode=? and msgid=?",
            chatd::OP_MSGUPD, msgid, mMessages.chatId(), item.rowid, chatd::OP_MSGUPDX, item.msg->id());
        assertAffectedRowCount(1, "updateSendingItemMsgidAndOpcode");
        commit();
    }
    virtual void deleteItemFromSending(uint64_t rowid)
    {
        sqliteQuery(mDb, "delete from sending where rowid = ?1", rowid);
        assertAffectedRowCount(1, "deleteItemFromSending");
        commit();
    }
    virtual void updateMsgPlaintextInSending(uint64_t rowid, const StaticBuffer& data)
    {
        sqliteQuery(mDb, "update sending set msg = ? where rowid = ?", data, rowid);
        assertAffectedRowCount(1, "updateMsgPlaintextInSending");
        commit();
    }
    virtual void updateMsgKeyIdInSending(uint64_t rowid, chatd::KeyId keyid)
    {
        sqliteQuery(mDb, "update sending set keyid = ? where rowid = ?", keyid, rowid);
        assertAffectedRowCount(1, "updateMsgKeyIdInSending");
        commit();
    }
    virtual void addMsgToHistory(const chatd::Message& msg, chatd::Idx idx)
    {
#if 1
        SqliteStmt stmt(mDb, "select min(idx), max(idx), count(*) from history where chatid = ?");
        stmt << mMessages.chatId();
        stmt.step();
        int low = stmt.intCol(0);
        int high = stmt.intCol(1);
        int count = stmt.intCol(2);
        if ((count > 0) && (idx != low-1) && (idx != high+1))
        {
            CHATD_LOG_ERROR("chatid %s(%" PRId64 "): addMsgToHistory: history discontinuity detected: "
                "index of added msg is not adjacent to neither end of db history: "
                "add idx=%d, histlow=%d, histhigh=%d, histcount= %d, fwdStart=%d",
                mMessages.chatId().toString().c_str(), mMessages.chatId().val,
                idx, low, high, count, mMessages.forwardStart());
            assert(false);
        }
#endif
        sqliteQuery(mDb, "insert into history"
            "(idx, chatid, msgid, keyid, type, userid, ts, updated, data, backrefid) "
            "values(?,?,?,?,?,?,?,?,?,?)", idx, mMessages.chatId(), msg.id(), msg.keyid,
            msg.type, msg.userid, msg.ts, msg.updated, msg, msg.backRefId);
        commit();
    }
    virtual void updateMsgInHistory(karere::Id msgid, const chatd::Message& msg)
    {
        sqliteQuery(mDb, "update history set type = ?, data = ?, updated = ? where chatid = ? and msgid = ?",
            msg.type, msg, msg.updated, mMessages.chatId(), msgid);
        assertAffectedRowCount(1, "updateMsgInHistory");
        commit();
    }
    virtual void loadSendQueue(chatd::Chat::OutputQueue& queue)
    {
        SqliteStmt stmt(mDb, "select rowid, opcode, msgid, keyid, msg, type, "
            "ts, updated, backrefid, backrefs, recipients from sending where chatid=? order by rowid asc");
        stmt << mMessages.chatId();
        queue.clear();
        while(stmt.step())
        {
            uint8_t opcode = stmt.intCol(1);

            assert((opcode == chatd::OP_NEWMSG) || (opcode == chatd::OP_MSGUPD)
                   || (opcode == chatd::OP_MSGUPDX));

            auto msg = new chatd::Message(stmt.int64Col(2), mMessages.client().userId(),
                    stmt.intCol(6), stmt.intCol(7), nullptr, 0, true, (chatd::KeyId)stmt.intCol(3),
                    (unsigned char)stmt.intCol(5));
            stmt.blobCol(4, *msg);
            msg->backRefId = stmt.uint64Col(8);
            if (stmt.hasBlobCol(9))
            {
                Buffer refs;
                stmt.blobCol(9, refs);
                refs.read(0, msg->backRefs);
            }
            Buffer recpts;
            stmt.blobCol(10, recpts);
            queue.emplace_back(opcode, msg, recpts, stmt.intCol(0));
        }
    }
    virtual void fetchDbHistory(chatd::Idx idx, unsigned count, std::vector<chatd::Message*>& messages)
    {
        SqliteStmt stmt(mDb, "select msgid, userid, ts, type, data, idx, keyid, backrefid, updated from history "
            "where chatid = ?1 and idx <= ?2 order by idx desc limit ?3");
        stmt << mMessages.chatId() << idx << count;
        int i = 0;
        while(stmt.step())
        {
            i++;
            karere::Id msgid(stmt.uint64Col(0));
            karere::Id userid(stmt.uint64Col(1));
            unsigned ts = stmt.uintCol(2);
            chatd::KeyId keyid = stmt.uintCol(6);
            Buffer buf;
            stmt.blobCol(4, buf);
#ifndef NDEBUG
            auto idx = stmt.intCol(5);
            if(idx != mMessages.lownum()-1-(int)messages.size()) //we go backward in history, hence the -messages.size()
            {
                CHATD_LOG_ERROR("chatid %s (%" PRId64 "): fetchDbHistory: History discontinuity detected: "
                    "expected idx %d, retrieved from db:%d", mMessages.chatId().toString().c_str(), mMessages.chatId().val,
                    mMessages.lownum()-1-(int)messages.size(), idx);
                assert(false);
            }
#endif
            auto msg = new chatd::Message(msgid, userid, ts, stmt.intCol(8), std::move(buf),
                false, keyid, (unsigned char)stmt.intCol(3));
            msg->backRefId = stmt.uint64Col(7);
            messages.push_back(msg);
        }
    }
    virtual chatd::Idx getIdxOfMsgid(karere::Id msgid)
    {
        SqliteStmt stmt(mDb, "select idx from history where chatid = ? and msgid = ?");
        stmt << mMessages.chatId() << msgid;
        return (stmt.step()) ? stmt.int64Col(0) : CHATD_IDX_INVALID;
    }
    virtual chatd::Idx getPeerMsgCountAfterIdx(chatd::Idx idx)
    {
        std::string sql = "select count(*) from history where (chatid = ?)"
                "and (userid != ?)";
        if (idx != CHATD_IDX_INVALID)
            sql+=" and (idx > ?)";

        SqliteStmt stmt(mDb, sql);
        stmt << mMessages.chatId() << mMessages.client().userId();
        if (idx != CHATD_IDX_INVALID)
            stmt << idx;
        stmt.stepMustHaveData("get peer msg count");
        return stmt.intCol(0);
    }
    virtual void saveItemToManualSending(const chatd::Chat::SendingItem& item, int reason)
    {
        auto& msg = *item.msg;
        sqliteQuery(mDb, "insert into manual_sending(chatid, rowid, msgid, type, "
            "ts, updated, msg, opcode, reason) values(?,?,?,?,?,?,?,?,?)",
            mMessages.chatId(), item.rowid, item.msg->id(), msg.type, msg.ts,
            msg.updated, msg, item.opcode(), reason);
        commit();
    }
    virtual void loadManualSendItems(std::vector<chatd::Chat::ManualSendItem>& items)
    {
        SqliteStmt stmt(mDb, "select rowid, msgid, type, ts, updated, msg, opcode, "
            "reason from manual_sending where chatid=? order by rowid asc");
        stmt << mMessages.chatId();
        while(stmt.step())
        {
            Buffer buf;
            stmt.blobCol(5, buf);
            auto msg = new chatd::Message(stmt.uint64Col(1), mMessages.client().userId(),
                stmt.int64Col(3), stmt.intCol(4), std::move(buf), true,
                CHATD_KEYID_INVALID, (unsigned char)stmt.intCol(2));
            items.emplace_back(msg, stmt.uint64Col(0), stmt.intCol(6), (chatd::ManualSendReason)stmt.intCol(7));
        }
    }
    virtual bool deleteManualSendItem(uint64_t rowid)
    {
        sqliteQuery(mDb, "delete from manual_sending where rowid = ?", rowid);
        commit();
        return sqlite3_changes(mDb) != 0;
    }
    virtual void truncateHistory(const chatd::Message& msg)
    {
        auto idx = getIdxOfMsgid(msg.id());
        if (idx == CHATD_IDX_INVALID)
            throw std::runtime_error("dbInterface::truncateHistory: msgid "+msg.id().toString()+" does not exist in db");
        sqliteQuery(mDb, "delete from history where chatid = ? and idx < ?", mMessages.chatId(), idx);
#if 1
        SqliteStmt stmt(mDb, "select type from history where chatid=? and msgid=?");
        stmt << mMessages.chatId() << msg.id();
        stmt.step();
        if (stmt.intCol(0) != chatd::Message::kMsgTruncate)
            throw std::runtime_error("DbInterface::truncateHistory: Truncate message type is not 'truncate'");
#endif
        commit();
    }
    virtual chatd::Idx getOldestIdx()
    {
        SqliteStmt stmt(mDb, "select min(idx) from history where chatid = ?");
        stmt << mMessages.chatId();
        stmt.stepMustHaveData(__FUNCTION__);
        return stmt.uint64Col(0);
    }
    virtual void setLastSeen(karere::Id msgid)
    {
        sqliteQuery(mDb, "update chats set last_seen=? where chatid=?", msgid, mMessages.chatId());
        assertAffectedRowCount(1);
        commit();
    }
    virtual void setLastReceived(karere::Id msgid)
    {
        sqliteQuery(mDb, "update chats set last_recv=? where chatid=?", msgid, mMessages.chatId());
        assertAffectedRowCount(1);
        commit();
    }
    virtual void setHaveAllHistory()
    {
        sqliteQuery(mDb,
            "insert or replace into chat_vars(chatid, name, value) "
            "values(?, 'have_all_history', '1')", mMessages.chatId());
        commit();
    }
    virtual bool haveAllHistory()
    {
        SqliteStmt stmt(mDb,
            "select value from chat_vars where chatid=? and name='have_all_history'");
        if (!stmt.step())
            return false;
        return stmt.stringCol(0) == "1";
    }
    virtual void getLastTextMessage(chatd::Idx from, chatd::LastTextMsgState& msg)
    {
        SqliteStmt stmt(mDb,
            "select type, idx, data, msgid, userid from history where chatid=? and "
            "(type=1 or type >= 16) and (idx <= ?) and length(data) > 0 "
            "order by idx desc limit 1");
        stmt << mMessages.chatId() << from;
        if (!stmt.step())
        {
            msg.clear();
            return;
        }
        Buffer buf(128);
        stmt.blobCol(2, buf);
        msg.assign(buf, stmt.intCol(0), stmt.uint64Col(3), stmt.intCol(1), stmt.uint64Col(4));
    }
};

#endif
