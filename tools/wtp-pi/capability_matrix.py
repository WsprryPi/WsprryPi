#!/usr/bin/env python3
"""Explicit native WTP LOAD/ABORT matrix; never sends ARM or starts RF."""
import argparse
import json
import uuid
from pathlib import Path
from probe import Wtp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--port', type=int, default=31417)
    parser.add_argument('--execute', action='store_true', help='Authorize ownership and LOAD/ABORT operations')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    if not args.execute:
        parser.error('Use --execute to authorize ownership and LOAD/ABORT')
    result = {'cases': [], 'starts_rf': False, 'passed': False}
    wire = Wtp(args.host, args.port)
    job_id = None
    try:
        result['caps'] = wire.request('CAPS')['body']
        assert set(result['caps']['modes']) == {'tone', 'wspr', 'qrss', 'fskcw', 'dfcw'}
        assert result['caps']['max_events'] == 512
        assert int(result['caps']['max_job_duration_ns']) == 86400000000000
        wire.request('CLAIM', {'owner_id': wire.owner, 'lease_ms': 60000})
        for hz in (137500, 475700, 1838100, 3570100, 5364700, 7040100,
                   10140200, 14097100, 18106100, 21096100, 24926100,
                   28126100, 50294500, 70092500, 144490000):
            for mode, count in [('tone', 1), ('wspr', 4), ('qrss', 1), ('fskcw', 2), ('dfcw', 2)]:
                job_id = uuid.uuid4().hex
                events = []
                for index in range(count):
                    events.append({'offset_ns': str(index * 2000000000),
                                   'duration_ns': '2000000000', 'rf_on': True,
                                   'frequency_nhz': str(hz * 1000000000 + index * 1464843750)})
                reply = wire.request('LOAD', {'job_id': job_id, 'profile': 'rf-events/1',
                    'mode': mode, 'total_duration_ns': str(count * 2000000000),
                    'allow_frequency_adjustment': True, 'events': events})
                stopped = wire.request('ABORT', {'job_id': job_id})
                result['cases'].append({'frequency_hz': hz, 'mode': mode,
                                        'load': reply, 'abort': stopped})
        # A message can use only one of the mode's possible tones.
        for mode in ('dfcw', 'fskcw', 'wspr'):
            job_id = uuid.uuid4().hex
            wire.request('LOAD', {'job_id': job_id, 'profile': 'rf-events/1', 'mode': mode,
                'total_duration_ns': '12000000001', 'allow_frequency_adjustment': True,
                'events': [{'offset_ns': '0', 'duration_ns': '1000000000', 'rf_on': False},
                           {'offset_ns': '1000000000', 'duration_ns': '11000000000',
                            'rf_on': True, 'frequency_nhz': '14097100000000000'},
                           {'offset_ns': '12000000000', 'duration_ns': '1', 'rf_on': False}]})
            wire.request('ABORT', {'job_id': job_id})
        result['partial_tone_set_and_silent_prefix_cases'] = 3
        result['passed'] = True
    except Exception as error:
        result['failure'] = str(error)
        raise
    finally:
        try:
            if job_id:
                wire.request('ABORT', {'job_id': job_id}, okay=False)
            wire.request('RELEASE', okay=False)
            result['final_status'] = wire.request('STATUS')
            assert result['final_status']['body']['output_active'] is False
        except Exception as error:
            result['passed'] = False
            result['cleanup_failure'] = str(error)
            raise
        finally:
            wire.close()
            Path(args.output).write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'passed': True, 'band_mode_cases': len(result['cases']),
                      'additional_cases': 3, 'starts_rf': False}))


if __name__ == '__main__':
    main()
