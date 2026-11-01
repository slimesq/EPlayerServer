#pragma once

#include "CServer.h"
#include "Crypto.h"
#include "HttpParser.h"
#include "Logger.h"
#include "MysqlClient.h"
#include "jsoncpp/json.h"
#include <atomic>
#include <memory>
#include <mutex>

DECLARE_TABLE_CLASS(edoyunLogin_user_mysql, _mysql_table_)
DECLARE_MYSQL_FIELD(
    TYPE_INT, user_id, NOT_NULL | PRIMARY_KEY | AUTOINCREMENT, "INTEGER", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_VARCHAR, user_qq, NOT_NULL, "VARCHAR", "(15)", "", "")  //QQ号
DECLARE_MYSQL_FIELD(
    TYPE_VARCHAR, user_phone, DEFAULT, "VARCHAR", "(11)", "'18888888888'", "")  //手机
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_name, NOT_NULL, "TEXT", "", "", "")         //姓名
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_nick, NOT_NULL, "TEXT", "", "", "")         //昵称
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_wechat, DEFAULT, "TEXT", "", "NULL", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_wechat_id, DEFAULT, "TEXT", "", "NULL", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_address, DEFAULT, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_province, DEFAULT, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_country, DEFAULT, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_INT, user_age, DEFAULT | CHECK, "INTEGER", "", "18", "")
DECLARE_MYSQL_FIELD(TYPE_INT, user_male, DEFAULT, "BOOL", "", "1", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_flags, DEFAULT, "TEXT", "", "0", "")
DECLARE_MYSQL_FIELD(TYPE_REAL, user_experience, DEFAULT, "REAL", "", "0.0", "")
DECLARE_MYSQL_FIELD(TYPE_INT, user_level, DEFAULT | CHECK, "INTEGER", "", "0", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_class_priority, DEFAULT, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_REAL, user_time_per_viewer, DEFAULT, "REAL", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_career, NONE, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_password, NOT_NULL, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_INT, user_birthday, NONE, "DATETIME", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_describe, NONE, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_TEXT, user_education, NONE, "TEXT", "", "", "")
DECLARE_MYSQL_FIELD(TYPE_INT, user_register_time, DEFAULT, "DATETIME", "", "CURRENT_TIMESTAMP", "")
DECLARE_TABLE_CLASS_EDN()

class CEdoyunPlayerServer : public CBusiness
{
public:
    explicit CEdoyunPlayerServer(unsigned count) : m_count(count ? count : 1)
    {
    }
    ~CEdoyunPlayerServer() override;
    int BusinessProcess(CProcess* process) override;

private:
    struct ClientState
    {
        std::shared_ptr<CSocketBase> socket;
        CHttpParser parser;
        Buffer request;
    };
    int Connected(CSocketBase* client);
    int Received(CSocketBase* client, Buffer const& data);
    int HttpParser(Buffer const& data);
    Buffer MakeResponse(int result);
    int ThreadFunc();
    void RemoveClient(int descriptor);
    CEpoll m_epoll;
    CThreadPool m_pool;
    unsigned m_count;
    std::unique_ptr<CMysqlClient> m_db;
    std::mutex m_databaseMutex;
    std::mutex m_clientsMutex;
    std::map<int, std::shared_ptr<ClientState>> m_mapClients;
    std::atomic<bool> m_stopping{false};
};
