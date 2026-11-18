# Independent standard-library fixture generation; never production crypto.
import base64,hashlib,json,pathlib
root=pathlib.Path(__file__).resolve().parent
encode=lambda x:base64.urlsafe_b64encode(x).rstrip(b'=').decode()
c={'contract':'Wsprry-Pair/1','schema':'hello','action':'pair','authorization_generation':'0','controller_id':'a'*32,'controller_boot_id':'b'*32,'target_id':'c'*32,'target_boot_id':'d'*32,'window_id':'e'*32,'pair_id':'f'*32,'controller_nonce':encode(bytes(range(32))),'target_nonce':encode(bytes(range(32,64))),'suite':'SPAKE2+-P256-SHA256-HKDF-HMAC'}
context=json.dumps(c,separators=(',',':'),sort_keys=True).encode();p=b'Wsprry-Pair/1/controller/'+context;v=b'Wsprry-Pair/1/target/'+context
framed=b''.join(len(x).to_bytes(8,'little')+x for x in (b'2345ABCD',p,v));salt=hashlib.sha256(context).digest();raw=hashlib.scrypt(framed,salt=salt,n=32768,r=8,p=1,dklen=96,maxmem=34*1024*1024)
order=int('ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551',16)
scalar=b''.join((int.from_bytes(x,'big')%order).to_bytes(32,'big') for x in (raw[:48],raw[48:]))
values={'context':context.decode(),'prover_id':p.decode(),'verifier_id':v.decode(),'framed_hex':framed.hex(),'salt_hex':salt.hex(),'derived_hex':raw.hex(),'scalars_hex':scalar.hex()}
(root/'profile-vector.json').write_text(json.dumps(values,indent=2)+'\n')
(root/'profile_vector.hpp').write_text('\n'.join('inline constexpr const char* '+k+' = '+json.dumps(v)+';' for k,v in values.items())+'\n')
