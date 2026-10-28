<?php
// SPDX-License-Identifier: MIT
// Explicit stream-v2 provenance. UDP claims never become local host identity.
declare(strict_types=1);
function log_v2_source($value): string {
    if (!is_string($value)) return 'local';
    return $value === 'all' || $value === 'local' || preg_match('/^(?:usb|pico):[0-9a-f]{32}$/D',$value) ||
        preg_match('/^udp:[A-Za-z0-9_-]{1,32}:(?:pico|pi):[0-9a-f]{32}(?::(?:direct|relay))?$/D',$value) ? $value : 'local';
}
function log_v2_scope_matches(array $entry): bool {
    if (in_array($entry['_SYSTEMD_UNIT'] ?? null,['wsprrypi.service','wsprrypi-log.service'],true)) return true;
    return in_array($entry['_PID'] ?? null,['1',1],true) &&
        (in_array($entry['UNIT'] ?? null,['wsprrypi.service','wsprrypi-log.service'],true) || in_array($entry['OBJECT_SYSTEMD_UNIT'] ?? null,['wsprrypi.service','wsprrypi-log.service'],true));
}
function log_v2_origin(array $entry): array {
    $get=static function(string $key,int $maximum=128) use($entry): ?string {
        $value=$entry[$key] ?? null;
        return is_string($value)&&strlen($value)<=$maximum&&!preg_match('/[\x00-\x1f\x7f]/',$value)?$value:null;
    };
    $hex=static function(string $key) use($get): ?string { $v=$get($key,32); return $v!==null&&preg_match('/^[0-9a-f]{32}$/D',$v)?$v:null; };
    $decimal=static function(string $key) use($get): ?string {
        $v=$get($key,20);return $v!==null&&preg_match('/^(0|[1-9][0-9]{0,19})$/D',$v)&&(strlen($v)<20||strcmp($v,'18446744073709551615')<=0)?$v:null;
    };
    $udp=($entry['_SYSTEMD_UNIT'] ?? null)==='wsprrypi-log.service'&&($entry['WSPRRY_INGRESS'] ?? null)==='udp';
    $usb=($entry['_SYSTEMD_UNIT'] ?? null)==='wsprrypi.service'&&($entry['WSPRRY_INGRESS'] ?? null)==='usb';
    $kind=in_array($get('WSPRRY_ORIGIN_KIND'),['pi','pico'],true)?$get('WSPRRY_ORIGIN_KIND'):null;
    $id=$hex('WSPRRY_ORIGIN_ID');$slot=$get('WSPRRY_PEER_SLOT',32);
    if ($slot===null||!preg_match('/^[A-Za-z0-9_-]{1,32}$/D',$slot)) $slot=null;
    $forwarders=$get('WSPRRY_FORWARDERS',256);$relay=false;
    if ($forwarders!==null) {
        $decoded=json_decode($forwarders,true);
        if (!is_array($decoded)||!array_is_list($decoded)||count($decoded)>1) $forwarders=null;
        else {
            $relay=count($decoded)===1;
            if($relay) {
                $r=$decoded[0];$keys=is_array($r)?array_keys($r):[];sort($keys);
                if($keys!==['id','ingress','receipt_us'] || !is_string($r['id']) || !preg_match('/^[0-9a-f]{32}$/D',$r['id']) || $r['ingress']!=='usb' || !is_string($r['receipt_us']) || !preg_match('/^(0|[1-9][0-9]{0,19})$/D',$r['receipt_us']) || (strlen($r['receipt_us'])===20&&strcmp($r['receipt_us'],'18446744073709551615')>0) || $kind!=='pico') {$forwarders=null;$relay=false;}
            }
        }
    }
    $base=log_origin($entry); // Legacy normalization only for trusted local presentation.
    $base['origin_kind']=$kind;$base['origin_id']=$id;$base['origin_name']=$get('WSPRRY_ORIGIN_NAME',63);
    $base['origin_boot']=$hex('WSPRRY_ORIGIN_BOOT');$base['origin_sequence']=$decimal('WSPRRY_ORIGIN_SEQUENCE');
    $base['origin_utc_ns']=$decimal('WSPRRY_ORIGIN_UTC_NS');$base['origin_monotonic_ns']=$decimal('WSPRRY_ORIGIN_MONOTONIC_NS');
    $base['source']=$udp&&$kind!==null&&$id!==null&&$slot!==null?'udp:'.$slot.':'.$kind.':'.$id.':'.($relay?'relay':'direct'):($usb&&$id!==null?'usb:'.$id:'local');
    if (!$udp&&!$usb&&($entry['_SYSTEMD_UNIT'] ?? null)==='wsprrypi.service') {
        $local=log_origin($entry);$base['origin_id']=$id??$local['origin_id'];$base['origin_name']=$base['origin_name']??$local['origin_name'];$base['origin_kind']='pi';
    }
    if (($udp&&($kind===null||$id===null||$slot===null))||(!$udp&&!$usb&&($entry['_SYSTEMD_UNIT']??null)!=='wsprrypi.service'))$base['source']='unknown';
    $base['ingress']=$udp?'udp':($usb?'usb':($get('WSPRRY_INGRESS')??'native'));
    $base['ingress_trust']=$udp?'unauthenticated_udp':($usb?'usb_identity_checked':'local_journal');
    $base['peer_slot']=$udp?$slot:null;$base['peer_ipv4']=$udp?$get('WSPRRY_PEER_IPV4',15):null;
    $base['peer_port']=$udp?$decimal('WSPRRY_PEER_PORT'):null;$base['forwarders']=$forwarders;
    return $base;
}
function log_v2_source_matches(array $entry,string $source): bool {
    if ($source==='all')return true;
    if ($source==='local')return ($entry['_SYSTEMD_UNIT']??null)==='wsprrypi.service'&&($entry['WSPRRY_INGRESS']??null)!=='usb'&&($entry['WSPRRY_ORIGIN_KIND']??null)!=='pico';
    $origin=log_v2_origin($entry);if(str_starts_with($source,'pico:'))$source='usb:'.substr($source,5);
    if ($origin['source']===$source)return true;
    return str_starts_with($source,'udp:')&&substr_count($source,':')===3&&str_starts_with($origin['source'],$source.':');
}
