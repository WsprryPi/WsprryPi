#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Finite browser request schedules for loopback production-app regressions."""
import time


def require(value,message):
    if not value: raise ValueError(message)


def browser_schedule(start,end,stop,get,clock=time.monotonic,grant_control=None):
    """Preserve 0.2 Hz status while spreading each reload's reads over its next slots."""
    status_at=page_at=start;pages=[];read_budget=0.0
    while clock()<end and not stop.is_set():
        now=clock()
        if now>=page_at:
            require(not pages,'Previous browser page reload incomplete')
            pages.extend(('/', '/version', '/config'));page_at+=30
        if now>=status_at:
            require(now-status_at<=1,'Browser nominal sampling fell behind')
            get('/api/wtp');status_at+=5
            slots_left=round((page_at-status_at)/5)+1
            # A completed status may already have consumed the next polling slot.
            # Keep its due successor ahead of reads or control; do not drop either.
            granted=(grant_control is not None and clock()<status_at and
                     len(pages)<slots_left and grant_control(status_at))
            # Do not create sampling debt by starting an asset that its measured
            # cost predicts will exceed the existing one-second status allowance. All reads must
            # still finish in their original thirty-second reload window.
            if pages and not granted and clock()<status_at and clock()+read_budget<=status_at+1:
                read_start=clock();get(pages.pop(0))
                read_budget=max(read_budget,clock()-read_start)
        stop.wait(max(0,min(.1,status_at-clock(),end-clock())))
    require(not pages,'Final browser page reload incomplete')


def normal_actions(seconds):
    """Finite ordinary-browser actions with explicit refresh and reload slots."""
    require(seconds in (180, 300), 'Normal browser interval is not frozen')
    refresh = (20, 40, 60, 100, 130, 160) if seconds == 180 else (30, 70, 110, 190, 230, 270)
    page = ('/', '/version', '/api/wtp', '/config')
    return sorted([(0, 'initialize', page), (seconds // 2, 'reload', page)] +
                  [(offset, 'refresh', ('/api/wtp',)) for offset in refresh])


def normal_browser_schedule(start, seconds, stop, get, note, clock=time.monotonic, grant_control=None):
    for index, (offset, action, paths) in enumerate(normal_actions(seconds)):
        due = start + offset
        while clock() < due and not stop.is_set():
            # Job mutations share the browser lane; admit only a complete
            # five-second request plus one second margin before the next action.
            if grant_control is not None and due - clock() >= 6:
                grant_control(min(due, clock() + 6))
            stop.wait(max(0, min(.1, due - clock())))
        require(not stop.is_set() and 0 <= clock() - due <= 15, 'Normal browser action late/stopped')
        began = clock()
        note('browser_action_start', dict(index=index, action=action,
            scheduled_monotonic_ns=int(due * 1e9), paths=list(paths)))
        for path in paths:
            require(not stop.is_set(), 'Normal browser action interrupted')
            get(path)
        require(clock() <= start + seconds and clock() - began <= len(paths) * 15,
                'Normal browser action deadline')
        note('browser_action_finish', dict(index=index, action=action))
    # Preserve the final gap for finite job completion/release.
    while grant_control is not None and clock() < start + seconds and not stop.is_set():
        if start + seconds - clock() >= 6:
            grant_control(min(start + seconds, clock() + 6))
        stop.wait(min(.1, max(0, start + seconds - clock())))
