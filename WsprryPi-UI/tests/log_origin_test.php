<?php
// SPDX-License-Identifier: MIT
require __DIR__ . '/../data/log_origin.php';
function check($value) { if (!$value) throw new RuntimeException('log origin assertion failed'); }
check(log_scope_matches(['_SYSTEMD_UNIT'=>'wsprrypi.service']));
check(log_scope_matches(['_SYSTEMD_UNIT'=>'init.scope','_PID'=>'1','UNIT'=>'wsprrypi.service']));
check(!log_scope_matches(['_SYSTEMD_UNIT'=>'other.service','_PID'=>'123','UNIT'=>'wsprrypi.service']));
$id = str_repeat('a',32);
$entry=['WSPRRY_ORIGIN_KIND'=>'pico','WSPRRY_ORIGIN_ID'=>$id,'WSPRRY_ORIGIN_BOOT'=>str_repeat('b',32),
    'WSPRRY_ORIGIN_SEQUENCE'=>'18446744073709551615','WSPRRY_ORIGIN_UTC_NS'=>'1793098800123456789',
    'WSPRRY_RECEIPT_US'=>'1793098800123456','WSPRRY_ORIGIN_NAME'=>'pico.local'];
check(log_source_matches($entry,'all')); check(!log_source_matches($entry,'local'));
check(log_source_matches($entry,'pico:'.$id));check(!log_source_matches($entry,'pico:'.str_repeat('c',32)));
$normal=log_origin($entry);check($normal['origin_sequence']==='18446744073709551615');
check($normal['origin_utc_ns']==='1793098800123456789');check($normal['receipt_us']==='1793098800123456');
check(log_source('*')==='local');check(log_source('pico:'.$id)==='pico:'.$id);
$entry['WSPRRY_ORIGIN_NAME']="injected\nline";check(log_origin($entry)['origin_name']===null);
$entry['WSPRRY_ORIGIN_KIND']='pi';check(log_source_matches($entry,'local'));check(log_origin($entry)['origin_boot']===str_repeat('b',32));
echo "Log origin PHP: source isolation, string counters and injection rejection passed\n";
