#!/usr/bin/env python3
"""Constructed public P62-04 HKDF fixture, not upstream audit evidence."""
import argparse
import base64
import hashlib
import hmac
import json
from pathlib import Path

root=Path(__file__).resolve().parent
b64=lambda value:base64.urlsafe_b64encode(value).decode().rstrip('=')
context={'purpose':'Wsprry-Pair/1/retirement-tls','target_id':'a'*32,'controller_id':'b'*32,
         'pair_id':'c'*32,'authorization_generation':'7','client_role':'target','server_role':'controller'}
canonical=json.dumps(context,sort_keys=True,separators=(',',':'),ensure_ascii=False)
salt=hashlib.sha256(canonical.encode()).digest()
verifier=bytes(range(32))
info=b'Wsprry-Pair/1/retirement-tls-psk'
extracted=hmac.new(salt,verifier,hashlib.sha256).digest()
key=hmac.new(extracted,info+b'\x01',hashlib.sha256).digest()
values={'context_json':canonical,'salt_hex':salt.hex(),'verifier':b64(verifier),'transport_psk':b64(key)}
document={'schema':'constructed-retirement-fixture/1',**values}
header='// Generated public software fixture; never operational credentials.\n#pragma once\n#include <string_view>\nnamespace retirement_fixture {\n'+''.join('inline constexpr std::string_view '+name+'=R"RET('+value+')RET";\n' for name,value in values.items())+'}\n'
outputs={root/'retirement-vector.json':json.dumps(document,indent=2)+'\n',root/'retirement-vector.hpp':header}
parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');args=parser.parse_args()
for path,value in outputs.items():
    if args.check:
        if not path.exists() or path.read_text()!=value:
            raise SystemExit('Stale constructed retirement fixture: '+path.name)
    else:
        path.write_text(value)
print('PASS constructed retirement fixture is current' if args.check else 'Generated constructed public retirement fixture')
