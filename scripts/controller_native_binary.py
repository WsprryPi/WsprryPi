#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline Mach-O and PE admission for controller application bundles."""

from pathlib import PurePosixPath
import posixpath
import re
import struct

from controller_binary import IDENTITY


PROFILES = {
    'macos-26': {'amd64': 0x01000007, 'arm64': 0x0100000C},
    'macos-27': {'arm64': 0x0100000C},
    'windows-26300': {'amd64': 0x8664},
}
SYSTEM_DLLS = set('advapi32 bcrypt bcryptprimitives cfgmgr32 comctl32 comdlg32 crypt32 cryptnet '
                  'dnsapi dwmapi gdi32 iphlpapi kernel32 kernelbase mpr msasn1 msvcrt ncrypt netapi32 '
                  'mswsock normaliz ntdll ole32 oleaut32 powrprof propsys psapi rpcrt4 secur32 setupapi shell32 shlwapi '
                  'user32 userenv ucrtbase version winhttp winmm winspool wintrust ws2_32 wtsapi32'.split())
VC_RUNTIME = set('concrt140.dll msvcp140.dll msvcp140_1.dll msvcp140_2.dll msvcp140_atomic_wait.dll '
                 'vcruntime140.dll vcruntime140_1.dll'.split())


def require(ok, message):
    if not ok:
        raise ValueError(message)


def fields(data, offset, fmt):
    require(0 <= offset <= len(data) - struct.calcsize(fmt), 'Truncated native image')
    return struct.unpack_from(fmt, data, offset)


def cstring(data, start, end):
    require(0 <= start < end <= len(data), 'Invalid native string bounds')
    stop = data.find(b'\0', start, end)
    require(start < stop and stop - start <= 512, 'Invalid native string')
    return data[start:stop].decode('ascii')


def mach(data, cpu, profile, name, inventory, main=False, executable=False):
    magic, machine, _, kind, count, size, _, _ = fields(data, 0, '<8I')
    require(magic == 0xFEEDFACF and machine == cpu and kind in (2, 6, 8)
            and (not (main or executable) or kind == 2) and 0 < count <= 1024 and size <= len(data) - 32,
            'Wrong Mach-O architecture, format, or type')
    end = 32 + size
    offset = 32
    versions, search, imports = [], [], []
    for _ in range(count):
        command, length = fields(data, offset, '<II')
        require(length >= 8 and length % 8 == 0 and offset + length <= end, 'Invalid Mach-O load command')
        if command == 0x32:
            require(length >= 24, 'Incomplete Mach-O build version')
            platform, minimum, _, _ = fields(data, offset + 8, '<4I')
            require(platform == 1, 'Mach-O is not built for macOS')
            versions.append(minimum)
        elif command == 0x24:
            require(length >= 16, 'Incomplete Mach-O minimum version')
            versions.append(fields(data, offset + 8, '<I')[0])
        elif command in (0xC, 0x80000018, 0x8000001F, 0x20, 0x80000023, 0xE, 0x8000001C):
            start = fields(data, offset + 8, '<I')[0]
            require(12 <= start < length, 'Invalid Mach-O dependency offset')
            value = cstring(data, offset + start, offset + length)
            if command == 0xE:
                require(value == '/usr/lib/dyld', 'Unexpected Mach-O dynamic loader')
            elif command == 0x8000001C:
                search.append(value)
            else:
                imports.append(value)
        offset += length
    selected = int(profile.removeprefix('macos-'))
    require(offset == end and len(versions) == 1 and 0 < versions[0] <= selected << 16
            and (not main or versions[0] == selected << 16), 'Wrong macOS deployment target')
    origin = posixpath.dirname(name)
    def local(value):
        marker, slash, rest = value.partition('/')
        require(slash and (marker == '@loader_path' or marker == '@executable_path' and kind == 2),
                'Nonrelocatable Mach-O dependency')
        path = posixpath.normpath(posixpath.join(origin, rest))
        require(not path.startswith(('/', '../')) and path != '..', 'Mach-O dependency escapes its bundle')
        return path

    rpaths = [local(path) for path in search]
    for library in imports:
        if library.startswith(('/usr/lib/', '/System/Library/')):
            require('/../' not in library and '/./' not in library, 'Unsafe system library path')
        elif library.startswith('@rpath/'):
            relative = library.removeprefix('@rpath/')
            require(relative and '..' not in PurePosixPath(relative).parts and not relative.startswith('/')
                    and any(posixpath.join(root, relative) in inventory for root in rpaths),
                    f'Unresolved Mach-O rpath dependency in {name}')
        else:
            require(local(library) in inventory, f'Missing Mach-O library for {name}')
    return sorted(set(imports))


def pe(data, cpu, name, inventory, executable=False):
    require(data.startswith(b'MZ'), 'Expected a PE image')
    header, = fields(data, 0x3C, '<I')
    marker, machine, sections, _, _, _, optional_size, characteristics = fields(data, header, '<4sHHIIIHH')
    require(marker == b'PE\0\0' and machine == cpu and 0 < sections <= 96 and optional_size >= 112
            and characteristics & 2 and (not executable or not characteristics & 0x2000),
            'Wrong PE architecture, format, or type')
    optional = header + 24
    require(fields(data, optional, '<H')[0] == 0x20B, 'Expected PE32+')
    table = optional + optional_size
    directories = min(fields(data, optional + 108, '<I')[0], (optional_size - 112) // 8)
    ranges = []
    for section in range(sections):
        _, _, virtual, size, raw = fields(data, table + section * 40, '<8sIIII')
        require(size == 0 or 0 < raw <= len(data) - size, 'Invalid PE section bounds')
        if size:
            ranges.append((virtual, virtual + size, raw))

    def address(rva):
        matches = [(raw + rva - start, raw + end - start) for start, end, raw in ranges if start <= rva < end]
        require(len(matches) == 1, 'Invalid PE import address')
        return matches[0]

    imports = set()
    for directory, width, name_index in ((1, 20, 3), (13, 32, 1)):
        if directory >= directories:
            continue
        pointer, length = fields(data, optional + 112 + directory * 8, '<II')
        if not pointer and not length:
            continue
        offset, end = address(pointer)
        require(width <= length <= end - offset and length <= 65536, 'Invalid PE import table')
        for index in range(length // width):
            row = fields(data, offset + index * width, '<' + 'I' * (width // 4))
            if not any(row):
                break
            require(directory != 13 or row[0] == 1, 'Unsupported PE delay-import address mode')
            dll = cstring(data, *address(row[name_index])).lower()
            origin = posixpath.dirname(name)
            apache_module = dll == 'mod_proxy.so' and origin == 'runtime/apache/modules'
            require(apache_module or re.fullmatch(r'[a-z0-9_.+-]+\.dll', dll), 'Unsafe PE dependency name')
            search = {origin.lower()}
            if origin.startswith(('runtime/apache/', 'runtime/php')):
                search.add('runtime/apache/bin')
            if origin.startswith('runtime/') and not origin.startswith(('runtime/apache/', 'runtime/php')):
                search.add('runtime')
            require(dll[:-4] in SYSTEM_DLLS or dll in VC_RUNTIME or dll.startswith(('api-ms-win-', 'ext-ms-win-'))
                    or any(posixpath.join(directory, dll) in inventory for directory in search),
                    f'Unresolved PE dependency {dll} in {name}')
            imports.add(dll)
        else:
            raise ValueError('Unterminated PE import table')
    return sorted(imports)


def inspect(path, profile, architecture, name, inventory, main=False, executable=False):
    cpu = PROFILES[profile][architecture]
    data = path.read_bytes()
    if profile.startswith('macos-'):
        imports = mach(data, cpu, profile, name, inventory, main, executable)
    else:
        imports = pe(data, cpu, name, {item.casefold() for item in inventory}, executable=main or executable)
    if not main:
        return imports
    records = re.findall(rb'(wsprrypi-controller/1 [\x20-\x7e]{16,300})\0', data)
    identities = [IDENTITY.fullmatch(record.decode('ascii')) for record in records]
    require(len(identities) == 1 and identities[0], 'Expected one native zero-output controller build identity')
    revision, version, dirty, inputs = identities[0].groups()
    return {'platform': profile, 'architecture': architecture, 'role': 'controller',
            'source_commit': revision, 'application_version': version,
            'tracked_source_dirty': dirty == 'true', 'controller_inputs_sha256': inputs,
            'native_libraries': imports}
