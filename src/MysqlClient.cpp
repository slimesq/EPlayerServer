#include "MysqlClient.h"
#include <sstream>
#include "Logger.h"

int CMysqlClient::Connect(KeyValue const& args)
{
    if (m_bInit)
        return -1;
    for (auto key : {"host", "user", "password", "db", "port"})
        if (args.find(key) == args.end())
            return -1;
    MYSQL* ret = mysql_init(&m_db);
    if (ret == NULL)
        return -2;
    unsigned timeout = 3;
    mysql_options(&m_db, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(&m_db, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    ret = mysql_real_connect(&m_db,
                             args.at("host"),
                             args.at("user"),
                             args.at("password"),
                             args.at("db"),
                             atoi(args.at("port")),
                             NULL,
                             0);
    if ((ret == NULL) && (mysql_errno(&m_db) != 0))
    {
        fprintf(stderr, "%s %d %s\n", __FUNCTION__, mysql_errno(&m_db), mysql_error(&m_db));
        TRACEE("%d %s", mysql_errno(&m_db), mysql_error(&m_db));
        mysql_close(&m_db);
        bzero(&m_db, sizeof(m_db));
        return -3;
    }
    m_bInit = true;
    return 0;
}

int CMysqlClient::Exec(Buffer const& sql)
{
    if (!m_bInit)
        return -1;
    int ret = mysql_real_query(&m_db, sql, sql.size());
    if (ret != 0)
    {
        fprintf(stderr, "%s %d %s\n", __FUNCTION__, mysql_errno(&m_db), mysql_error(&m_db));
        TRACEE("%d %s", mysql_errno(&m_db), mysql_error(&m_db));
        return -2;
    }
    return 0;
}

int CMysqlClient::Exec(Buffer const& sql, Result& result, _Table_ const& table)
{
    if (!m_bInit)
        return -1;
    int ret = mysql_real_query(&m_db, sql, sql.size());
    if (ret != 0)
    {
        fprintf(stderr, "%s %d %s\n", __FUNCTION__, mysql_errno(&m_db), mysql_error(&m_db));
        TRACEE("%d %s", mysql_errno(&m_db), mysql_error(&m_db));
        return -2;
    }
    MYSQL_RES* res = mysql_store_result(&m_db);
    if (!res)
        return -2;
    MYSQL_FIELD* fields = mysql_fetch_fields(res);
    MYSQL_ROW row;
    unsigned count = mysql_num_fields(res);
    while ((row = mysql_fetch_row(res)))
    {
        PTable record = table.Copy();
        unsigned long* lengths = mysql_fetch_lengths(res);
        for (unsigned i = 0; i < count; ++i)
        {
            auto field = record->Fields.find(fields[i].name);
            if (field != record->Fields.end() && row[i])
                field->second->LoadFromStr(Buffer(row[i], lengths[i]));
        }
        result.push_back(std::move(record));
    }
    mysql_free_result(res);
    return 0;
}

int CMysqlClient::StartTransaction()
{
    if (!m_bInit)
        return -1;
    int ret = mysql_real_query(&m_db, "BEGIN", 5);
    if (ret != 0)
    {
        fprintf(stderr, "%s %d %s\n", __FUNCTION__, mysql_errno(&m_db), mysql_error(&m_db));
        TRACEE("%d %s", mysql_errno(&m_db), mysql_error(&m_db));
        return -2;
    }
    return 0;
}

int CMysqlClient::CommitTransaction()
{
    if (!m_bInit)
        return -1;
    int ret = mysql_real_query(&m_db, "COMMIT", 6);
    if (ret != 0)
    {
        fprintf(stderr, "%s %d %s\n", __FUNCTION__, mysql_errno(&m_db), mysql_error(&m_db));
        TRACEE("%d %s", mysql_errno(&m_db), mysql_error(&m_db));
        return -2;
    }
    return 0;
}

int CMysqlClient::RollbackTransaction()
{
    if (!m_bInit)
        return -1;
    int ret = mysql_real_query(&m_db, "ROLLBACK", 8);
    if (ret != 0)
    {
        fprintf(stderr, "%s %d %s\n", __FUNCTION__, mysql_errno(&m_db), mysql_error(&m_db));
        TRACEE("%d %s", mysql_errno(&m_db), mysql_error(&m_db));
        return -2;
    }
    return 0;
}

int CMysqlClient::Close()
{
    if (m_bInit)
    {
        m_bInit = false;
        mysql_close(&m_db);
        bzero(&m_db, sizeof(m_db));
    }
    return 0;
}

bool CMysqlClient::IsConnected()
{
    return m_bInit;
}

_mysql_table_::_mysql_table_(_mysql_table_ const& table)
{
    Database = table.Database;
    Name = table.Name;
    for (size_t i = 0; i < table.FieldDefine.size(); i++)
    {
        PField field = PField(new _mysql_field_(*(_mysql_field_*)table.FieldDefine[i].get()));
        FieldDefine.push_back(field);
        Fields[field->Name] = field;
    }
}

_mysql_table_::~_mysql_table_()
{
}

Buffer _mysql_table_::Create()
{  //CREATE TABLE IF NOT EXISTS 表全名 (列定义,..., PRIMARY KEY `主键列名` ,UNIQUE INDEX `列名_UNIQUE` (列名 ASC) VISIBLE );
    Buffer sql = "CREATE TABLE IF NOT EXISTS " + (Buffer) * this + " (\r\n";
    for (unsigned i = 0; i < FieldDefine.size(); i++)
    {
        if (i > 0)
            sql += ",\r\n";
        sql += FieldDefine[i]->Create();
        if (FieldDefine[i]->Attr & PRIMARY_KEY)
        {
            sql += ",\r\n PRIMARY KEY (`" + FieldDefine[i]->Name + "`)";
        }
        if (FieldDefine[i]->Attr & UNIQUE)
        {
            sql += ",\r\n UNIQUE INDEX `" + FieldDefine[i]->Name + "_UNIQUE` (";
            sql += (Buffer)*FieldDefine[i] + " ASC) VISIBLE ";
        }
    }
    sql += ");";
    return sql;
}

Buffer _mysql_table_::Drop()
{
    return "DROP TABLE " + (Buffer) * this + ";";
}

Buffer _mysql_table_::Insert(_Table_ const& values)
{  // INSERT INTO 表全名 (列名,...)VALUES(值,...);
    Buffer sql = "INSERT INTO " + (Buffer) * this + " (";
    bool isfirst = true;
    for (size_t i = 0; i < values.FieldDefine.size(); i++)
    {
        if (values.FieldDefine[i]->Condition & SQL_INSERT)
        {
            if (!isfirst)
                sql += ",";
            else
                isfirst = false;
            sql += (Buffer)*values.FieldDefine[i];
        }
    }
    sql += ") VALUES (";
    isfirst = true;
    for (size_t i = 0; i < values.FieldDefine.size(); i++)
    {
        if (values.FieldDefine[i]->Condition & SQL_INSERT)
        {
            if (!isfirst)
                sql += ",";
            else
                isfirst = false;
            sql += values.FieldDefine[i]->toSqlStr();
        }
    }
    sql += " );";
    printf("sql = %s\n", (char*)sql);
    return sql;
}

Buffer _mysql_table_::Delete(_Table_ const& values)
{
    Buffer sql = "DELETE FROM " + (Buffer) * this + " ";
    Buffer Where = "";
    bool isfirst = true;
    for (size_t i = 0; i < values.FieldDefine.size(); i++)
    {
        if (values.FieldDefine[i]->Condition & SQL_CONDITION)
        {
            if (!isfirst)
                Where += " AND ";
            else
                isfirst = false;
            Where += (Buffer)*values.FieldDefine[i] + "=" + values.FieldDefine[i]->toSqlStr();
        }
    }
    if (Where.size() > 0)
        sql += " WHERE " + Where;
    sql += ";";
    printf("sql = %s\r\n", (char*)sql);
    return sql;
}

Buffer _mysql_table_::Modify(_Table_ const& values)
{
    Buffer sql = "UPDATE " + (Buffer) * this + " SET ";
    bool isfirst = true;
    for (size_t i = 0; i < values.FieldDefine.size(); i++)
    {
        if (values.FieldDefine[i]->Condition & SQL_MODIFY)
        {
            if (!isfirst)
                sql += ",";
            else
                isfirst = false;
            sql += (Buffer)*values.FieldDefine[i] + "=" + values.FieldDefine[i]->toSqlStr();
        }
    }

    Buffer Where = "";
    isfirst = true;
    for (size_t i = 0; i < values.FieldDefine.size(); i++)
    {
        if (values.FieldDefine[i]->Condition & SQL_CONDITION)
        {
            if (!isfirst)
                Where += " AND ";
            else
                isfirst = false;
            Where += (Buffer)*values.FieldDefine[i] + "=" + values.FieldDefine[i]->toSqlStr();
        }
    }
    if (Where.size() > 0)
        sql += " WHERE " + Where;
    sql += " ;";
    printf("sql = %s\n", (char*)sql);
    return sql;
}

Buffer _mysql_table_::Query(Buffer const& condition)
{
    Buffer sql = "SELECT ";
    for (size_t i = 0; i < FieldDefine.size(); i++)
    {
        if (i > 0)
            sql += ',';
        sql += '`' + FieldDefine[i]->Name + "` ";
    }
    sql += " FROM " + (Buffer) * this + " ";
    if (condition.size() > 0)
    {
        sql += " WHERE " + condition;
    }
    sql += ";";
    printf("sql = %s\n", (char*)sql);
    return sql;
}

PTable _mysql_table_::Copy() const
{
    return PTable(new _mysql_table_(*this));
}

void _mysql_table_::ClearFieldUsed()
{
    for (size_t i = 0; i < FieldDefine.size(); i++)
    {
        FieldDefine[i]->Condition = 0;
    }
}

_mysql_table_::operator Buffer const() const
{
    Buffer Head;
    if (Database.size())
        Head = '`' + Database + "`.";
    return Head + '`' + Name + '`';
}

_mysql_field_::_mysql_field_() : _Field_()
{
    nType = TYPE_NULL;
    Value.Double = 0.0;
}

_mysql_field_::_mysql_field_(int ntype,
                             Buffer const& name,
                             unsigned attr,
                             Buffer const& type,
                             Buffer const& size,
                             Buffer const& default_,
                             Buffer const& check)
{
    nType = ntype;
    switch (ntype)
    {
        case TYPE_VARCHAR:
        case TYPE_TEXT:
        case TYPE_BLOB:
            Value.String = new Buffer();
            break;
    }

    Name = name;
    Attr = attr;
    Type = type;
    Size = size;
    Default = default_;
    Check = check;
}

_mysql_field_::_mysql_field_(_mysql_field_ const& field) : _Field_()
{
    nType = field.nType;
    Value = field.Value;
    Condition = field.Condition;
    switch (field.nType)
    {
        case TYPE_VARCHAR:
        case TYPE_TEXT:
        case TYPE_BLOB:
            Value.String = new Buffer();
            *Value.String = *field.Value.String;
            break;
    }

    Name = field.Name;
    Attr = field.Attr;
    Type = field.Type;
    Size = field.Size;
    Default = field.Default;
    Check = field.Check;
}

_mysql_field_::~_mysql_field_()
{
    switch (nType)
    {
        case TYPE_VARCHAR:
        case TYPE_TEXT:
        case TYPE_BLOB:
            if (Value.String)
            {
                Buffer* p = Value.String;
                Value.String = NULL;
                delete p;
            }
            break;
    }
}

Buffer _mysql_field_::Create()
{
    Buffer sql = "`" + Name + "` " + Type + Size + " ";
    if (Attr & (NOT_NULL | PRIMARY_KEY))
    {
        sql += "NOT NULL";
    }
    else
    {
        sql += "NULL";
    }
    //BLOB TEXT GEOMETRY JSON不能有默认值的
    if ((Attr & DEFAULT) && (Default.size() > 0) && (Type != "BLOB") && (Type != "TEXT") &&
        (Type != "GEOMETRY") && (Type != "JSON"))
    {
        sql += " DEFAULT " + Default + " ";
    }
    //UNIQUE PRIMARY_KEY 外面处理
    //CHECK mysql不支持
    if (Attr & AUTOINCREMENT)
    {
        sql += " AUTO_INCREMENT ";
    }
    return sql;
}

void _mysql_field_::LoadFromStr(Buffer const& str)
{
    switch (nType)
    {
        case TYPE_NULL:
            break;
        case TYPE_BOOL:
        case TYPE_INT:
        case TYPE_DATETIME:
            Value.Integer = atoi(str);
            break;
        case TYPE_REAL:
            Value.Double = atof(str);
            break;
        case TYPE_VARCHAR:
        case TYPE_TEXT:
            *Value.String = str;
            break;
        case TYPE_BLOB:
            *Value.String = str;
            break;
        default:
            printf("type=%d\n", nType);
            break;
    }
}

Buffer _mysql_field_::toEqualExp() const
{
    return (Buffer) * this + (nType == TYPE_NULL ? " IS NULL" : " = " + toSqlStr());
}

Buffer _mysql_field_::toSqlStr() const
{
    switch (nType)
    {
        case TYPE_NULL:
            return "NULL";
        case TYPE_BOOL:
        case TYPE_INT:
        case TYPE_DATETIME:
            return std::to_string(Value.Integer);
        case TYPE_REAL:
        {
            std::ostringstream out;
            out.precision(17);
            out << Value.Double;
            return out.str();
        }
        case TYPE_VARCHAR:
        case TYPE_TEXT:
        {
            return "CONVERT(X'" + Str2Hex(*Value.String) + "' USING utf8mb4)";
        }
        case TYPE_BLOB:
            return "X'" + Str2Hex(*Value.String) + "'";
        default:
            return "NULL";
    }
}

_mysql_field_::operator Buffer const() const
{
    return '`' + Name + '`';
}

Buffer _mysql_field_::Str2Hex(Buffer const& data) const
{
    char const* hex = "0123456789ABCDEF";
    std::stringstream ss;
    for (auto ch : data)
        ss << hex[(unsigned char)ch >> 4] << hex[(unsigned char)ch & 0xF];
    return ss.str();
}

Buffer CMysqlClient::Escape(Buffer const& value)
{
    if (!m_bInit)
        return {};
    Buffer escaped(value.size() * 2 + 1);
    escaped.resize(
        mysql_real_escape_string_quote(&m_db, escaped.data(), value.data(), value.size(), '\''));
    return escaped;
}
