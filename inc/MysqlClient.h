#pragma once
#pragma once
#include "Public.h"
#include "DatabaseHelper.h"
#include <mysql/mysql.h>
#include <cstring>
#include <strings.h>

class CMysqlClient : public CDatabaseClient
{
public:
    CMysqlClient(CMysqlClient const&) = delete;
    CMysqlClient& operator=(CMysqlClient const&) = delete;

public:
    CMysqlClient()
    {
        bzero(&m_db, sizeof(m_db));
        m_bInit = false;
    }
    virtual ~CMysqlClient()
    {
        Close();
    }

public:
    //连接
    virtual int Connect(KeyValue const& args);
    //执行
    virtual int Exec(Buffer const& sql);
    //带结果的执行
    virtual int Exec(Buffer const& sql, Result& result, _Table_ const& table);
    //开启事务
    virtual int StartTransaction();
    //提交事务
    virtual int CommitTransaction();
    //回滚事务
    virtual int RollbackTransaction();
    //关闭连接
    virtual int Close();
    //是否连接 true表示连接中 false表示未连接
    virtual bool IsConnected();
    Buffer Escape(Buffer const& value);

private:
    MYSQL m_db;
    bool m_bInit;  //默认是false 表示没有初始化 初始化之后，则为true，表示已经连接
private:
    class ExecParam
    {
    public:
        ExecParam(CMysqlClient* obj, Result& result, _Table_ const& table)
            : obj(obj), result(result), table(table)
        {
        }
        CMysqlClient* obj;
        Result& result;
        _Table_ const& table;
    };
};

class _mysql_table_ : public _Table_
{
public:
    _mysql_table_() : _Table_()
    {
    }
    _mysql_table_(_mysql_table_ const& table);
    virtual ~_mysql_table_();
    //返回创建的SQL语句
    virtual Buffer Create();
    //删除表
    virtual Buffer Drop();
    //增删改查
    //TODO:参数进行优化
    virtual Buffer Insert(_Table_ const& values);
    virtual Buffer Delete(_Table_ const& values);
    //TODO:参数进行优化
    virtual Buffer Modify(_Table_ const& values);
    virtual Buffer Query(Buffer const& condition = "");
    //创建一个基于表的对象
    virtual PTable Copy() const;
    virtual void ClearFieldUsed();

public:
    //获取表的全名
    virtual operator Buffer const() const;
};

class _mysql_field_ : public _Field_
{
public:
    _mysql_field_();
    _mysql_field_(int ntype,
                  Buffer const& name,
                  unsigned attr,
                  Buffer const& type,
                  Buffer const& size,
                  Buffer const& default_,
                  Buffer const& check);
    _mysql_field_(_mysql_field_ const& field);
    virtual ~_mysql_field_();
    virtual Buffer Create();
    virtual void LoadFromStr(Buffer const& str);
    //where 语句使用的
    virtual Buffer toEqualExp() const;
    virtual Buffer toSqlStr() const;
    //列的全名
    virtual operator Buffer const() const;

private:
    Buffer Str2Hex(Buffer const& data) const;
};

#define DECLARE_TABLE_CLASS(name, base)     \
    class name : public base                \
    {                                       \
    public:                                 \
        virtual PTable Copy() const         \
        {                                   \
            return PTable(new name(*this)); \
        }                                   \
        name() : base()                     \
        {                                   \
            Name = #name;

#define DECLARE_MYSQL_FIELD(ntype, name, attr, type, size, default_, check)               \
    {                                                                                     \
        PField field(new _mysql_field_(ntype, #name, attr, type, size, default_, check)); \
        FieldDefine.push_back(field);                                                     \
        Fields[#name] = field;                                                            \
    }

#define DECLARE_TABLE_CLASS_EDN() \
    }                             \
    }                             \
    ;
