#!/usr/bin/env python3
"""Read-only live API and mDNS observer for the discovery campaign."""
import argparse, concurrent.futures, json, socket, struct, threading, time, urllib.request
from pathlib import Path

def name(data, offset, depth=0):
    if depth > 16: raise ValueError('DNS pointer depth')
    parts=[]
    while True:
        size=data[offset]; offset+=1
        if size == 0: return '.'.join(parts)+'.', offset
        if size & 192 == 192:
            pointer=((size & 63)<<8)|data[offset]
            suffix,_=name(data,pointer,depth+1)
            return '.'.join(parts)+('.' if parts else '')+suffix,offset+1
        if size > 63: raise ValueError('DNS label')
        parts.append(data[offset:offset+size].decode('utf-8','replace')); offset+=size

def decode(data):
    if len(data)<12: return None
    ident,flags,questions,answers,authority,additional=struct.unpack_from('!6H',data)
    offset=12; query=[]; records=[]
    for _ in range(questions):
        owner,offset=name(data,offset); kind,cls=struct.unpack_from('!HH',data,offset); offset+=4
        query.append({'name':owner,'type':kind,'class':cls})
    for _ in range(answers+authority+additional):
        owner,offset=name(data,offset); kind,cls,ttl,length=struct.unpack_from('!HHIH',data,offset); offset+=10
        raw=data[offset:offset+length]
        value=None
        if kind in (12,5): value=name(data,offset)[0]
        elif kind == 33:
            priority,weight,port=struct.unpack_from('!HHH',data,offset)
            value={'priority':priority,'weight':weight,'port':port,'target':name(data,offset+6)[0]}
        elif kind==1 and length==4: value=socket.inet_ntop(socket.AF_INET,raw)
        elif kind==16:
            value=[]; pos=0
            while pos<len(raw):
                size=raw[pos];pos+=1;value.append(raw[pos:pos+size].decode('utf-8','replace'));pos+=size
        records.append({'name':owner,'type':kind,'class':cls,'ttl':ttl,'value':value})
        offset+=length
    return {'response':bool(flags & 32768),'flags':flags,'questions':query,'records':records}

def get(resource):
    with urllib.request.urlopen('http://127.0.0.1:31415/api/v1/host/'+resource,timeout=3) as response:
        return json.load(response)

def main():
    p=argparse.ArgumentParser();p.add_argument('--directory',required=True);p.add_argument('--seconds',type=int,default=1800)
    p.add_argument('--wire',action='store_true');a=p.parse_args()
    directory=Path(a.directory);directory.mkdir(parents=True,exist_ok=True)
    stop=threading.Event();end=time.monotonic()+a.seconds
    def capture():
        sock=socket.socket(socket.AF_PACKET,socket.SOCK_RAW,socket.htons(3));sock.settimeout(1)
        with (directory/'dns.jsonl').open('a') as out:
            while not stop.is_set():
                try:
                    packet,link=sock.recvfrom(65535)
                    if len(packet)<42 or packet[12:14]!=b'\x08\x00' or packet[23]!=17: continue
                    ihl=(packet[14]&15)*4;udp=14+ihl
                    source_port,destination_port,length=struct.unpack_from('!HHH',packet,udp)
                    if source_port!=5353 and destination_port!=5353:continue
                    data=packet[udp+8:udp+length];parsed=decode(data)
                    if not parsed or not any('_wtp.' in x['name'].lower() or 'wtp-' in x['name'].lower() or 'discovery-expiry' in x['name'].lower() for x in parsed['questions']+parsed['records']):continue
                    row={'utc_ns':str(time.time_ns()),'monotonic_ns':str(time.monotonic_ns()),'interface':link[0],
                         'packet_type':link[2],'source':socket.inet_ntoa(packet[26:30]),'destination':socket.inet_ntoa(packet[30:34]),
                         'source_port':source_port,'destination_port':destination_port,'dns':parsed,'wire_hex':data.hex()}
                    out.write(json.dumps(row,separators=(',',':'))+'\n');out.flush()
                except socket.timeout:pass
                except (ValueError,IndexError,struct.error):continue
        sock.close()
    thread=threading.Thread(target=capture) if a.wire else None
    if thread:thread.start()
    with (directory/'api.jsonl').open('a') as out:
        index=0
        while time.monotonic()<end:
            row={'utc_ns':str(time.time_ns()),'monotonic_ns':str(time.monotonic_ns())}
            for resource in (['discovery','wtp-endpoint','fleet'] if index%10==0 else ['discovery','wtp-endpoint']):
                try:row[resource]=get(resource)
                except Exception as error:row[resource]={'error':str(error)}
            out.write(json.dumps(row,separators=(',',':'))+'\n');out.flush();index+=1;time.sleep(.5)
    stop.set()
    if thread:thread.join(3)

if __name__=='__main__': main()
