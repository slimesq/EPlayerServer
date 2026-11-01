# EPlayerServer

基于 Linux 的 C++ 登录服务，使用主进程接收 TCP 连接，将 socket 传给业务子进程，由线程池处理 HTTP 登录请求；日志由独立子进程写入 `log/`。

项目讲义见 [EPlayerServer 系统学习路线](docs/README.md)，按概念、接口参数、状态推演、源码映射和资源生命周期展开，重点解释项目架构与设计模式。

`inc/` 和 `src/` 包含进程、线程池、epoll、TCP 和 Unix socket、日志、HTTP 解析、MySQL 与 SQLite 数据访问及登录业务。`third_party/` 保留参考源码自带的 http-parser、JsonCpp 和 SQLite，`examples/fd_demo.cpp` 保留原项目的文件描述符传递示例。

## 构建

Ubuntu 22.04 使用 GCC 11、CMake 3.22、OpenSSL 3 和 MySQL 8 客户端库：

```bash
sudo apt install build-essential cmake libssl-dev libmysqlclient-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

MySQL 客户端也可以安装在用户目录。CMake 会检查 `~/.local/opt/eplayer-mysql-deps/usr`；其他路径通过 `-DEPLAYER_MYSQL_ROOT=/安装前缀` 指定。构建会生成 `build/compile_commands.json`，供 clangd 使用。

## 本机数据库和启动

默认连接 `127.0.0.1:3306`，数据库为 `edoyun`。先在 MySQL 中创建数据库，并给应用账号相应权限：

```sql
CREATE DATABASE IF NOT EXISTS edoyun CHARACTER SET utf8mb4;
```

将 `.env.example` 复制为 `.env`，填写 `EPLAYER_DB_USER` 和 `EPLAYER_DB_PASSWORD`。本机已有 `.env` 时直接编辑它；该文件已被 Git 忽略。含空格或 shell 特殊字符的值用单引号包围。

命令行从项目根目录启动：

```bash
set -a
source .env
set +a
./build/EPlayerServer --host 127.0.0.1 --port 9999 --workers 4
```

程序默认监听 `0.0.0.0:9999`，支持 `--help`。数据库连接成功后会自动创建缺失的 `edoyunLogin_user_mysql` 表，账号数据需要自行导入。使用 Ctrl+C 停止服务，日志和业务子进程随之退出。

## 登录接口

请求格式为 `GET /login?time=时间戳&salt=随机值&user=用户名&sign=签名`，参数需要 URL 编码。签名沿用参考项目协议：`MD5(time + key + user_password + salt)`，结果为 32 位十六进制字符串。`key` 默认沿用参考代码中的值，也可通过 `EPLAYER_SIGN_KEY` 设置。

响应为 HTTP 200，JSON 中的 `status` 为 `0` 表示成功，`message` 为 `success`；失败时 `status` 为负数。该源码提供登录校验接口，未包含客户端、注册接口或视频流服务。

## VS Code 调试和格式化

以 EPlayerServer 为工作区打开 VS Code，编辑 `.env` 后按 F5，选择 `EPlayerServer (GDB)`。调试配置会先构建 Debug 目标，并载入 `.env`。默认调试主进程，登录处理位于业务子进程；需要调试业务时，在 GDB 中设置 `follow-fork-mode child`，并跳过第一次日志进程的 fork，或使用附加到进程调试业务子进程。

代码跳转和保存格式化由已有 clangd 21.1.8 配置提供，格式遵循 `.clang-format`。新增源码后重新运行 CMake，更新编译数据库。

## 测试

`ctest` 覆盖 HTTP 分段解析、URL 解码、MD5、进程间传递描述符、socket、epoll、线程池、日志和 SQLite 操作，无需外部数据库。

安装了 `mysql` 和 `mysqld` 后，可执行完整集成测试：

```bash
python3 tests/integration.py build/EPlayerServer
```

脚本在临时目录启动专用 MySQL 实例，验证建表、MySQL 增删改查与事务、登录成功及失败、中文用户名、SQL 转义、分段 HTTP、100 次并发登录请求和子进程退出。测试不会连接本机已有的 MySQL 服务，结束后删除测试数据。
