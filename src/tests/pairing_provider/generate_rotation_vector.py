#!/usr/bin/env python3
"""Constructed public rotation fixture, not an upstream conformance vector.

Scalars 1/2 use the standard P-256 generator and its double. Both key pairs
are checked independently by OpenSSL in the C++ fixture. HKDF is calculated
with Python's standard HMAC/SHA256, without production code or curve arithmetic.
No value here is an operational credential.
"""
import argparse,base64,hashlib,hmac,json,pathlib
root=pathlib.Path(__file__).resolve().parent
b64=lambda value:base64.urlsafe_b64encode(value).decode().rstrip('=')
a=bytes.fromhex('04'+'6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296'+'4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5')
b=bytes.fromhex('04'+'7cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978'+'07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1')
assert len(a)==len(b)==65
prepare={'contract':'Wsprry-Pair/1','schema':'rotate-prepare','operation_id':'d'*32,'pair_id':'c'*32,'expected_epoch':'7','pA':b64(a),'nonce':b64(bytes(range(32)))}
prepared={'contract':'Wsprry-Pair/1','schema':'rotate-prepared','operation_id':'d'*32,'next_epoch':'8','pB':b64(b),'nonce':b64(bytes(range(32,64)))}
canonical=lambda value:json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False)
info='Wsprry-Pair/1/rotation/'+canonical([prepare,prepared])
psk=bytes(range(64,96));shared=b[1:33]
extracted=hmac.new(psk,shared,hashlib.sha256).digest()
next_psk=hmac.new(extracted,info.encode()+b'\x01',hashlib.sha256).digest()
values={'controller_private':b64((1).to_bytes(32,'big')),'target_private':b64((2).to_bytes(32,'big')),'current_psk':b64(psk),'next_psk':b64(next_psk),'shared_x_hex':shared.hex(),'prepare_json':canonical(prepare),'prepared_json':canonical(prepared),'info_utf8':info}
document={'schema':'constructed-rotation-fixture/1',**values}
header='// Generated public software fixture; never operational credentials.\n#pragma once\n#include <string_view>\nnamespace rotation_fixture {\n'+''.join('inline constexpr std::string_view '+name+'=R"ROT('+value+')ROT";\n' for name,value in values.items())+'}\n'
outputs={root/'rotation-vector.json':json.dumps(document,indent=2)+'\n',root/'rotation-vector.hpp':header}
parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');args=parser.parse_args()
for path,value in outputs.items():
 if args.check:
  if not path.exists() or path.read_text()!=value:raise SystemExit('Stale constructed rotation fixture: '+path.name)
 else:path.write_text(value)
print('PASS constructed rotation fixture is current' if args.check else 'Generated constructed public rotation fixture')
