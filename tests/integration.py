#!/usr/bin/env python3
"""Start a private MySQL instance and exercise the complete login server.

Requires mysqld and mysql. All data is stored in a temporary directory;
no connection is made to the machine's existing MySQL instance.
"""
import argparse
import concurrent.futures
import hashlib
import http.client
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import urllib.parse

SIGN_KEY = '*&^%$#@b.v+h-b*g/h@n!h#n$d^ssx,.kl<kl'


def free_port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


def wait_for(predicate, process, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f'Process exited early: {process.returncode}')
        if predicate():
            return
        time.sleep(0.05)
    raise TimeoutError('Service did not become ready')


def stop(process):
    if process and process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise RuntimeError('Service required SIGKILL')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('server', type=Path)
    parser.add_argument('--datadir', type=Path, help='Reuse a private initialized test datadir')
    args = parser.parse_args()
    server = args.server.resolve()
    mysql = shutil.which('mysql')
    mysqld = shutil.which('mysqld') or '/usr/sbin/mysqld'
    if not mysql or not Path(mysqld).exists():
        parser.error('mysql and mysqld must be installed')
    with tempfile.TemporaryDirectory(prefix='eplayer-integration-') as temporary:
        root = Path(temporary)
        data = args.datadir.resolve() if args.datadir else root / 'mysql-data'
        mysql_log = root / 'mysql.log'
        if not args.datadir:
            subprocess.run([mysqld, '--no-defaults', '--initialize-insecure', f'--datadir={data}',
                            f'--log-error={mysql_log}'], check=True, timeout=90)
        db_port, http_port = free_port(), free_port()
        db = service = None
        try:
            db = subprocess.Popen([
                mysqld, '--no-defaults', f'--datadir={data}', f'--port={db_port}',
                '--bind-address=127.0.0.1', f'--socket={root / "mysql.sock"}',
                f'--pid-file={root / "mysql.pid"}', f'--log-error={mysql_log}',
                '--mysqlx=OFF', '--skip-log-bin', '--performance-schema=OFF',
                '--innodb-buffer-pool-size=32M', '--innodb-redo-log-capacity=32M',
                '--max-connections=30', '--skip-name-resolve'])

            def sql(statement):
                return subprocess.run([
                    mysql, '--no-defaults', '--protocol=SOCKET', f'--socket={root / "mysql.sock"}',
                    '--user=root', '--batch', '--skip-column-names',
                    '--default-character-set=utf8mb4', f'--execute={statement}'
                ], capture_output=True, text=True, timeout=5)

            wait_for(lambda: sql('SELECT 1').returncode == 0, db)
            result = sql("CREATE DATABASE IF NOT EXISTS edoyun CHARACTER SET utf8mb4; "
                         "CREATE USER IF NOT EXISTS 'root'@'127.0.0.1' IDENTIFIED BY ''; "
                         "GRANT ALL PRIVILEGES ON edoyun.* TO 'root'@'127.0.0.1'")
            assert result.returncode == 0, result.stderr
            environment = dict(os.environ, EPLAYER_DB_HOST='127.0.0.1',
                               EPLAYER_DB_PORT=str(db_port), EPLAYER_DB_USER='root',
                               EPLAYER_DB_PASSWORD='', EPLAYER_DB_NAME='edoyun',
                               EPLAYER_SIGN_KEY=SIGN_KEY, EPLAYER_TEST_MYSQL_PORT=str(db_port))
            component_tests = server.with_name('eplayer_tests')
            if component_tests.exists():
                subprocess.run([str(component_tests)], env=environment, check=True, timeout=30)
            with (root / 'server.log').open('w+') as output:
                service = subprocess.Popen([str(server), '--host', '127.0.0.1', '--port', str(http_port),
                                            '--workers', '4'], cwd=root, env=environment,
                                           stdout=output, stderr=subprocess.STDOUT, start_new_session=True)

                def listening():
                    try:
                        with socket.create_connection(('127.0.0.1', http_port), timeout=0.2):
                            return True
                    except OSError:
                        return False

                wait_for(listening, service)
                result = sql("INSERT INTO edoyun.edoyunLogin_user_mysql "
                             "(user_qq,user_name,user_nick,user_password) VALUES "
                             "('12345','testuser','tester','test-password'),"
                             "('67890','测试用户','unicode','test-password')")
                assert result.returncode == 0, result.stderr

                def path(user='testuser', valid=True):
                    timestamp, salt = '1700000000', 'a+b c'
                    signature = hashlib.md5((timestamp + SIGN_KEY + 'test-password' + salt).encode()).hexdigest()
                    return '/login?' + urllib.parse.urlencode(dict(time=timestamp, salt=salt, user=user,
                                                                   sign=signature if valid else '0' * 32))

                def request(target, method='GET'):
                    connection = http.client.HTTPConnection('127.0.0.1', http_port, timeout=5)
                    try:
                        connection.request(method, target)
                        response = connection.getresponse()
                        body = response.read()
                        assert response.status == 200
                        assert int(response.getheader('Content-Length')) == len(body)
                        assert response.getheader('Content-Type').startswith('application/json')
                        return json.loads(body)['status']
                    finally:
                        connection.close()

                assert request(path()) == 0
                assert request(path('测试用户')) == 0
                assert request(path(valid=False)) == -6
                assert request(path('missing')) == -4
                assert request(path("' OR 1=1 -- ")) == -4
                assert request('/login') == -6
                assert request('/other') == -7
                assert request(path(), 'POST') == -7
                with socket.create_connection(('127.0.0.1', http_port), timeout=5) as client:
                    payload = f'GET {path()} HTTP/1.1\r\nHost: localhost\r\n\r\n'.encode()
                    for index in range(0, len(payload), 7):
                        client.sendall(payload[index:index + 7])
                        time.sleep(0.002)
                    response = b''
                    while True:
                        chunk = client.recv(4096)
                        if not chunk:
                            break
                        response += chunk
                    assert json.loads(response.split(b'\r\n\r\n', 1)[1])['status'] == 0
                with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
                    results = list(pool.map(lambda _: request(path()), range(100)))
                assert results == [0] * 100
                children_path = Path(f'/proc/{service.pid}/task/{service.pid}/children')
                children = [int(pid) for pid in children_path.read_text().split()]
                assert len(children) == 2, children
                os.killpg(service.pid, signal.SIGINT)
                service.wait(timeout=8)
                assert service.returncode == 0, service.returncode
                assert all(not Path(f'/proc/{pid}').exists() for pid in children), 'Unreaped child process'
                assert not (root / 'log/server.sock').exists(), 'Stale logger socket'
                print('PASS: MySQL schema, login success/failure, Unicode, SQL escaping, '
                      'fragmented HTTP, 100 concurrent logins and Ctrl+C process-group shutdown')
        except Exception:
            if (root / 'server.log').exists():
                print((root / 'server.log').read_text()[-8000:])
            if mysql_log.exists():
                print(mysql_log.read_text()[-3000:])
            raise
        finally:
            try:
                stop(service)
            finally:
                stop(db)


if __name__ == '__main__':
    main()
