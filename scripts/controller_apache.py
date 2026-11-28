#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Render the fixed browser proxy for the supported controller hosts."""

import argparse
import json
from pathlib import PurePosixPath

from controller_bundle import APACHE_MODULES, require


HTTP_PORT = 8080


def quoted(value):
    value = str(value).replace('\\', '/')
    require(not any(ord(char) < 32 or char in ('"', '$') for char in value), 'Unsafe Apache path')
    return f'"{value}"'


def proxy_config(document_root, web, socket):
    require(type(web) is int and type(socket) is int and 1024 <= web <= 49151
            and 1024 <= socket <= 49151 and web != socket, 'Invalid controller proxy ports')
    lines = [
        '# Managed by the WsprryPi controller installer',
        f'Alias "/wsprrypi/" {quoted(str(document_root) + "/")}',
        f'<Directory {quoted(document_root)}>',
        '    Options FollowSymLinks',
        '    AllowOverride None',
        '    DirectoryIndex index.php',
        '    Require all granted',
        '    <FilesMatch "^(?:fetch_spots|log_stream|view_diag_logs)\\.php$">',
        '        Require all denied',
        '    </FilesMatch>',
        '</Directory>',
        '<LocationMatch "^/wsprrypi/(?:version$|status$|api(?:/|$)|socket$)">',
        '    ProxyPreserveHost On',
        '    ProxyAddHeaders Off',
        '    RequestHeader unset X-WsprryPi-Client-Address',
        '    RequestHeader set X-WsprryPi-Client-Address "expr=%{CONN_REMOTE_ADDR}"',
        '</LocationMatch>',
        '    # BEGIN WsprryPi proxy configuration',
    ]
    for path in ('version', 'status', 'api/control/v1', 'api/pair/v1', 'api/recovery'):
        url = f'http://127.0.0.1:{web}/{path}'
        lines.extend((f'    ProxyPass /wsprrypi/{path} {url}', f'    ProxyPassReverse /wsprrypi/{path} {url}'))
    lines.extend((f'    ProxyPass /wsprrypi/socket ws://127.0.0.1:{socket}/socket',
                  f'    ProxyPassReverse /wsprrypi/socket ws://127.0.0.1:{socket}/socket',
                  '    # END WsprryPi proxy configuration'))
    return '\n'.join(lines) + '\n'


def native_config(program, configuration, logs, platform, proxy=None, runtime=None):
    windows = platform == 'windows-26300'
    program = PurePosixPath(str(program).replace('\\', '/'))
    configuration = PurePosixPath(str(configuration).replace('\\', '/'))
    logs = PurePosixPath(str(logs).replace('\\', '/'))
    modules = program / 'runtime/apache/modules'
    items = [
        f'ServerRoot {quoted(program / "runtime/apache")}',
        'ServerName localhost',
        f'Listen {HTTP_PORT}',
        f'PidFile {quoted((PurePosixPath(str(runtime)) if runtime else logs) / "httpd.pid")}',
        f'ErrorLog {quoted(logs / "error.log")}',
        'LogLevel warn',
        'ProxyRequests Off',
        'ServerTokens Prod',
        'ServerSignature Off',
        'TraceEnable Off',
        'Timeout 90',
    ]
    if not windows:
        items.extend(('User _wsprrypihttp', 'Group _wsprrypihttp'))
    for name in (() if windows else ('mpm_prefork', 'unixd')) + APACHE_MODULES:
        items.append(f'LoadModule {name}_module {quoted(modules / f"mod_{name}.so")}')
    items.append(f'TypesConfig {quoted(program / "runtime/apache/conf/mime.types")}')
    php = program / 'runtime/php'
    if windows:
        items.append(f'LoadFile {quoted(php / "php8ts.dll")}')
    items.extend((
        f'LoadModule php_module {quoted(php / ("php8apache2_4.dll" if windows else "libphp.so"))}',
        f'PHPIniDir {quoted(configuration)}',
        '<Directory />',
        '    AllowOverride None',
        '    Require all denied',
        '</Directory>',
        f'<Directory {quoted(program / "ui")}>',
        '    <FilesMatch "\\.php$">',
        '        SetHandler application/x-httpd-php',
        '    </FilesMatch>',
        '</Directory>',
        f'Include {quoted(proxy or configuration / "proxy.conf")}',
    ))
    return '\n'.join(items) + '\n'


PHP_CONFIG = (
    'expose_php = Off\n'
    'display_errors = Off\n'
    'log_errors = On\n'
    'allow_url_include = Off\n'
    'file_uploads = Off\n'
)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', required=True)
    parser.add_argument('--configuration', required=True)
    parser.add_argument('--logs', required=True)
    parser.add_argument('--proxy', required=True)
    parser.add_argument('--web', type=int, required=True)
    parser.add_argument('--socket', type=int, required=True)
    args = parser.parse_args()
    print(json.dumps({
        'http': native_config(args.program, args.configuration, args.logs, 'windows-26300', args.proxy),
        'proxy': proxy_config(args.program + '/ui', args.web, args.socket),
        'php': PHP_CONFIG,
    }))
