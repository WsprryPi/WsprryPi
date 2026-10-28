#!/usr/bin/env python3
"""Embed the dedicated unprivileged worker in the verified application artifact."""
import hashlib
from pathlib import Path
import sys
if len(sys.argv)!=3:
    raise SystemExit('usage: embed_udp_worker.py <ELF|unavailable> <header>')
source, destination=sys.argv[1:]
data=b'' if source=='unavailable' else Path(source).read_bytes()
if data and (not data.startswith(b'\x7fELF') or len(data)>4*1024*1024):
    raise SystemExit('invalid logging worker ELF')
text=('#pragma once\n#include <array>\n#include <cstdint>\n'
      'inline constexpr std::array<std::uint8_t,'+str(len(data))+'> udp_worker_image={\n'+
      '\n'.join(','.join(str(b) for b in data[i:i+32])+',' for i in range(0,len(data),32))+'\n};\n'+
      'inline constexpr char udp_worker_digest[]="'+hashlib.sha256(data).hexdigest()+'";\n')
path=Path(destination);path.parent.mkdir(parents=True,exist_ok=True)
if not path.exists() or path.read_text()!=text:
    path.write_text(text)
