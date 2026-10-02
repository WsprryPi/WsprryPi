#!/usr/bin/env python3
"""GPIO management regression only; never requests output or WTP ownership."""
import concurrent.futures, hashlib, json, pathlib, subprocess, time, urllib.request

ROOT = pathlib.Path('/home/pi/si5351-inventory-20261002')
BASE = 'http://127.0.0.1:31415'


def request(path, body=None, revision=None):
    headers = {'Origin': BASE, 'X-WsprryPico-Request': '1'}
    if body is not None:
        headers['Content-Type'] = 'application/json'
    if revision:
        headers['If-Match'] = revision
    start = time.monotonic()
    req = urllib.request.Request(BASE + path, data=None if body is None else json.dumps(body).encode(),
                                 headers=headers, method='GET' if body is None else 'PUT')
    with urllib.request.urlopen(req, timeout=4) as reply:
        raw = reply.read().decode()
        return {'seconds': time.monotonic() - start, 'status': reply.status,
                'etag': reply.headers.get('ETag'), 'body': json.loads(raw) if raw.startswith('{') else raw}


def endpoint():
    state = request('/api/v1/host/wtp-endpoint')['body']
    assert not any(state.get(key) for key in ('local_requested', 'local_effective', 'remote_owner',
                                             'remote_output_active', 'output_unknown', 'local_work_active'))
    return state


def main():
    result = {'started_utc_ns': str(time.time_ns()), 'initial_endpoint': endpoint()}
    original = pathlib.Path('/usr/local/etc/wsprrypi.ini').read_bytes()
    initial = request('/api/v1/host/config')
    initial['body'] = initial['body']['config']
    assert initial['body']['Operation']['Transmit Backend'] == 'gpio'
    assert initial['body']['Operation']['Transmit'] is False
    assert not initial['body']['Platform']['Si5351 Inventory Checked']
    assert 'Si5351 Detected' not in initial['body']['Platform']
    result['initial_config_seconds'] = initial['seconds']
    result['original_ini_sha256'] = hashlib.sha256(original).hexdigest()
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            scans = [pool.submit(request, '/config/si5351-addresses?bus=1') for _ in range(2)]
            reads = [pool.submit(request, '/api/v1/host/config' if i % 2 == 0 else '/api/v1/host/discovery')
                     for i in range(40)]
            changed = request('/api/v1/host/config', {'WSPR': {'Use Random Offset': not initial['body']['WSPR']['Use Random Offset']}}, initial['etag'])
            restored = request('/api/v1/host/config', {'WSPR': {'Use Random Offset': initial['body']['WSPR']['Use Random Offset']}}, changed['etag'])
            result['writes'] = [{k: v for k, v in item.items() if k != 'body'} for item in (changed, restored)]
            result['reads'] = [{k: v for k, v in future.result().items() if k != 'body'} for future in reads]
            result['scans'] = [future.result() for future in scans]
        assert max(item['seconds'] for item in result['reads']) < 1.0
        assert max(item['seconds'] for item in result['writes']) < 1.0
        assert max(item['seconds'] for item in result['scans']) < 2.5
        result['final_endpoint'] = endpoint()
        assert result['initial_endpoint']['boot_id'] == result['final_endpoint']['boot_id']
        result['success'] = True
    finally:
        # Restore exact bytes; both transactions keep local Enable off throughout.
        temporary = pathlib.Path('/usr/local/etc/wsprrypi.ini.inventory-restore')
        temporary.write_bytes(original)
        temporary.chmod(0o644)
        temporary.replace('/usr/local/etc/wsprrypi.ini')
        result['restored_ini_sha256'] = hashlib.sha256(pathlib.Path('/usr/local/etc/wsprrypi.ini').read_bytes()).hexdigest()
        assert result['restored_ini_sha256'] == result['original_ini_sha256']
        (ROOT / 'live-result.json').write_text(json.dumps(result, indent=2) + '\n')
    time.sleep(2)
    result['after_restore_endpoint'] = endpoint()
    result['final_config_read_seconds'] = request('/api/v1/host/config')['seconds']
    result['service'] = subprocess.check_output(['systemctl', 'show', 'wsprrypi', '--property=ActiveState,SubState,MainPID'], text=True)
    result['remaining_inventory_workers'] = subprocess.run(['pgrep', '-af', '^/proc/self/exe --internal-si5351-inventory'], text=True, capture_output=True).stdout
    assert not result['remaining_inventory_workers']
    (ROOT / 'live-result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ('success', 'initial_config_seconds', 'final_config_read_seconds', 'scans', 'service')}, indent=2))


if __name__ == '__main__':
    main()
