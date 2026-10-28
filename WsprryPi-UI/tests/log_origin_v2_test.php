<?php
// SPDX-License-Identifier: MIT
require __DIR__.'/../data/log_origin.php';
function verify($value) {if(!$value)throw new RuntimeException('stream-v2 provenance assertion failed');}
$id=str_repeat('a',32);$boot=str_repeat('b',32);$collector=str_repeat('c',32);
$entry=['_SYSTEMD_UNIT'=>'wsprrypi-log.service','_HOSTNAME'=>'collector','_BOOT_ID'=>$collector,'__CURSOR'=>'real-cursor',
 'WSPRRY_INGRESS'=>'udp','WSPRRY_PEER_SLOT'=>'member-1','WSPRRY_PEER_IPV4'=>'192.0.2.10','WSPRRY_PEER_PORT'=>'45123',
 'WSPRRY_ORIGIN_KIND'=>'pi','WSPRRY_ORIGIN_ID'=>$id,'WSPRRY_ORIGIN_BOOT'=>$boot,'WSPRRY_ORIGIN_SEQUENCE'=>'1','WSPRRY_FORWARDERS'=>'[]'];
$source='udp:member-1:pi:'.$id;
verify(!log_scope_matches($entry,1)&&log_scope_matches($entry,2));
verify(log_cursor_matches($entry,'real-cursor',$source,2));verify(!log_cursor_matches($entry,'real-cursor','local',2));
verify(!log_cursor_matches($entry,'invented',$source,2));verify(!log_source_matches($entry,'udp:other:pi:'.$id,2));
$origin=log_origin($entry,2);verify($origin['origin_name']===null&&$origin['origin_boot']===$boot&&$origin['source']===$source.':direct');
unset($entry['WSPRRY_ORIGIN_ID'],$entry['WSPRRY_ORIGIN_BOOT']);$origin=log_origin($entry,2);
verify($origin['source']==='unknown'&&$origin['origin_id']===null&&$origin['origin_boot']===null&&$origin['origin_name']===null);
$entry['WSPRRY_ORIGIN_ID']=$id;$entry['WSPRRY_ORIGIN_KIND']='pico';$entry['WSPRRY_FORWARDERS']=json_encode([['id'=>$collector,'receipt_us'=>'123','ingress'=>'usb']]);
verify(log_source_matches($entry,'udp:member-1:pico:'.$id.':relay',2));verify(!log_source_matches($entry,'usb:'.$id,2));
$entry['_SYSTEMD_UNIT']='wsprrypi.service';$entry['WSPRRY_INGRESS']='usb';verify(log_source_matches($entry,'usb:'.$id,2));verify(!log_source_matches($entry,'local',2));
$entry['WSPRRY_ORIGIN_SEQUENCE']='18446744073709551616';verify(log_origin($entry,2)['origin_sequence']===null);
$entry['_SYSTEMD_UNIT']='other.service';verify(!log_scope_matches($entry,2));
verify(!log_scope_matches(['_SYSTEMD_UNIT'=>'other.service','UNIT'=>'wsprrypi-log.service','_PID'=>'12'],2));
verify(log_scope_matches(['_SYSTEMD_UNIT'=>'init.scope','UNIT'=>'wsprrypi-log.service','_PID'=>'1'],2));
echo "Logs stream v2: trusted units, remote/null origin, transport scopes and cursor isolation passed\n";
