#include "Sqlite3Client.h"
#include "Logger.h"

int CSqlite3Client::Connect(KeyValue const& args)
{
    auto it = args.find("host");
    if (it == args.end())
        return -1;
    if (m_db != NULL)
        return -2;
    int ret = sqlite3_open(it->second, &m_db);
    if (ret != 0)
    {
        TRACEE("connect failed:%d [%s]", ret, sqlite3_errmsg(m_db));
        return -3;
    }
    return 0;
}

int CSqlite3Client::Exec(Buffer const& sql)
{
    printf("sql={%s}\n", (char*)sql);
    if (m_db == NULL)
        return -1;
    int ret = sqlite3_exec(m_db, sql, NULL, this, NULL);
    if (ret != SQLITE_OK)
    {
        printf("sql={%s}\n", (char*)sql);
        printf("Exec failed:%d [%s]\n", ret, sqlite3_errmsg(m_db));
        return -2;
    }
    return 0;
}

int CSqlite3Client::Exec(Buffer const& sql, Result& result, _Table_ const& table)
{
    if (!m_db)
        return -1;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.data(), int(sql.size()), &statement, nullptr) != SQLITE_OK)
        return -2;
    int status;
    while ((status = sqlite3_step(statement)) == SQLITE_ROW)
    {
        PTable record = table.Copy();
        for (int i = 0; i < sqlite3_column_count(statement); ++i)
        {
            auto field = record->Fields.find(sqlite3_column_name(statement, i));
            if (field == record->Fields.end() || sqlite3_column_type(statement, i) == SQLITE_NULL)
                continue;
            auto* value = static_cast<char const*>(sqlite3_column_blob(statement, i));
            field->second->LoadFromStr(
                Buffer(value ? value : "", sqlite3_column_bytes(statement, i)));
        }
        result.push_back(std::move(record));
    }
    sqlite3_finalize(statement);
    return status == SQLITE_DONE ? 0 : -2;
}

int CSqlite3Client::StartTransaction()
{
    if (m_db == NULL)
        return -1;
    int ret = sqlite3_exec(m_db, "BEGIN TRANSACTION", 0, 0, NULL);
    if (ret != SQLITE_OK)
    {
        TRACEE("sql={BEGIN TRANSACTION}");
        TRACEE("BEGIN failed:%d [%s]", ret, sqlite3_errmsg(m_db));
        return -2;
    }
    return 0;
}

int CSqlite3Client::CommitTransaction()
{
    if (m_db == NULL)
        return -1;
    int ret = sqlite3_exec(m_db, "COMMIT TRANSACTION", 0, 0, NULL);
    if (ret != SQLITE_OK)
    {
        TRACEE("sql={COMMIT TRANSACTION}");
        TRACEE("COMMIT failed:%d [%s]", ret, sqlite3_errmsg(m_db));
        return -2;
    }
    return 0;
}

int CSqlite3Client::RollbackTransaction()
{
    if (m_db == NULL)
        return -1;
    int ret = sqlite3_exec(m_db, "ROLLBACK TRANSACTION", 0, 0, NULL);
    if (ret != SQLITE_OK)
    {
        TRACEE("sql={ROLLBACK TRANSACTION}");
        TRACEE("ROLLBACK failed:%d [%s]", ret, sqlite3_errmsg(m_db));
        return -2;
    }
    return 0;
}

int CSqlite3Client::Close()
{
    if (m_db == NULL)
        return -1;
    int ret = sqlite3_close(m_db);
    if (ret != SQLITE_OK)
    {
        TRACEE("Close failed:%d [%s]", ret, sqlite3_errmsg(m_db));
        return -2;
    }
    m_db = NULL;
    return 0;
}

bool CSqlite3Client::IsConnected()
{
    return m_db != NULL;
}

int CSqlite3Client::ExecCallback(void* arg, int count, char** values, char** names)
{
    ExecParam* param = (ExecParam*)arg;
    return param->obj->ExecCallback(param->result, param->table, count, names, values);
}

int CSqlite3Client::ExecCallback(
    Result& result, _Table_ const& table, int count, char** names, char** values)
{
    PTable pTable = table.Copy();
    if (pTable == nullptr)
    {
        printf("table %s error!\n", (char const*)(Buffer)table);
        return -1;
    }
    for (int i = 0; i < count; i++)
    {
        Buffer name = names[i];
        auto it = pTable->Fields.find(name);
        if (it == pTable->Fields.end())
        {
            printf("table %s error!\n", (char const*)(Buffer)table);
            return -2;
        }
        if (values[i] != NULL)
            it->second->LoadFromStr(values[i]);
    }
    result.push_back(pTable);
    return 0;
}

_sqlite3_table_::_sqlite3_table_(_sqlite3_table_ const& table)
{
    Database = table.Database;
    Name = table.Name;
    for (size_t i = 0; i < table.FieldDefine.size(); i++)
    {
        PField field = PField(new _sqlite3_field_(*(_sqlite3_field_*)table.FieldDefine[i].get()));
        FieldDefine.push_back(field);
        Fields[field->Name] = field;
    }
}

_sqlite3_table_::~_sqlite3_table_()
{
}

Buffer _sqlite3_table_::Create()
{  //CREATE TABLE IF NOT EXISTS 表全名 (列定义,……);
    //表全名 = 数据库.表名
    Buffer sql = "CREATE TABLE IF NOT EXISTS " + (Buffer) * this + "(\r\n";
    for (size_t i = 0; i < FieldDefine.size(); i++)
    {
        if (i > 0)
            sql += ",";
        sql += FieldDefine[i]->Create();
    }
    sql += ");";
    TRACEI("sql = %s", (char*)sql);
    return sql;
}

Buffer _sqlite3_table_::Drop()
{
    Buffer sql = "DROP TABLE " + (Buffer) * this + ";";
    TRACEI("sql = %s", (char*)sql);
    return sql;
}

Buffer _sqlite3_table_::Insert(_Table_ const& values)
{  //INSERT INTO 表全名 (列1,...,列n)
    //VALUES(值1,...,值n);
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
    TRACEI("sql = %s", (char*)sql);
    return sql;
}

Buffer _sqlite3_table_::Delete(_Table_ const& values)
{  // DELETE FROM 表全名 WHERE 条件
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
    TRACEI("sql = %s", (char*)sql);
    return sql;
}

Buffer _sqlite3_table_::Modify(_Table_ const& values)
{
    //UPDATE 表全名 SET 列1=值1 , ... , 列n=值n [WHERE 条件];
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
    TRACEI("sql = %s", (char*)sql);
    return sql;
}

Buffer _sqlite3_table_::Query(Buffer const& condition)
{  //SELECT 列名1 ,列名2 ,... ,列名n FROM 表全名;
    Buffer sql = "SELECT ";
    for (size_t i = 0; i < FieldDefine.size(); i++)
    {
        if (i > 0)
            sql += ',';
        sql += '"' + FieldDefine[i]->Name + "\" ";
    }
    sql += " FROM " + (Buffer) * this + " ";
    if (condition.size() > 0)
    {
        sql += " WHERE " + condition;
    }
    sql += ";";
    TRACEI("sql = %s", (char*)sql);
    return sql;
}

PTable _sqlite3_table_::Copy() const
{
    return PTable(new _sqlite3_table_(*this));
}

void _sqlite3_table_::ClearFieldUsed()
{
    for (size_t i = 0; i < FieldDefine.size(); i++)
    {
        FieldDefine[i]->Condition = 0;
    }
}

_sqlite3_table_::operator Buffer const() const
{
    Buffer Head;
    if (Database.size())
        Head = '"' + Database + "\".";
    return Head + '"' + Name + '"';
}

_sqlite3_field_::_sqlite3_field_() : _Field_()
{
    nType = TYPE_NULL;
    Value.Double = 0.0;
}

_sqlite3_field_::_sqlite3_field_(int ntype,
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

_sqlite3_field_::_sqlite3_field_(_sqlite3_field_ const& field) : _Field_()
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

_sqlite3_field_::~_sqlite3_field_()
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

Buffer _sqlite3_field_::Create()
{  //"名称" 类型 属性
    Buffer sql = '"' + Name + "\" " + Type + " ";
    if (Attr & NOT_NULL)
    {
        sql += " NOT NULL ";
    }
    if ((Attr & DEFAULT) && !Default.empty())
    {
        sql += " DEFAULT " + Default + " ";
    }
    if (Attr & UNIQUE)
    {
        sql += " UNIQUE ";
    }
    if (Attr & PRIMARY_KEY)
    {
        sql += " PRIMARY KEY ";
    }
    if ((Attr & CHECK) && !Check.empty())
    {
        sql += " CHECK( " + Check + ") ";
    }
    if (Attr & AUTOINCREMENT)
    {
        sql += " AUTOINCREMENT ";
    }
    return sql;
}

void _sqlite3_field_::LoadFromStr(Buffer const& str)
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
            TRACEW("type=%d", nType);
            break;
    }
}

Buffer _sqlite3_field_::toEqualExp() const
{
    return (Buffer) * this + (nType == TYPE_NULL ? " IS NULL" : " = " + toSqlStr());
}

Buffer _sqlite3_field_::toSqlStr() const
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
            Buffer quoted = "'";
            for (char character : *Value.String)
            {
                if (character == '\'')
                    quoted += '\'';
                quoted += character;
            }
            return quoted + "'";
        }
        case TYPE_BLOB:
            return "X'" + Str2Hex(*Value.String) + "'";
        default:
            return "NULL";
    }
}

_sqlite3_field_::operator Buffer const() const
{
    return '"' + Name + '"';
}

Buffer _sqlite3_field_::Str2Hex(Buffer const& data) const
{
    char const* hex = "0123456789ABCDEF";
    std::stringstream ss;
    for (auto ch : data)
        ss << hex[(unsigned char)ch >> 4] << hex[(unsigned char)ch & 0xF];
    return ss.str();
}
