# WsprryPi Planner Preference QA Checklist

## 1. Build & Automated Tests
- [ ] Run full rebuild:
  ```bash
  make -B -j$(nproc)
  ```
- [ ] Run semantics tests:
  ```bash
  make -B semantics-test
  ```
- [ ] Confirm all tests pass

---

## 2. INI Persistence

Test each option via UI save and verify `/usr/local/etc/wsprrypi.ini`.

### Automatic
```
[Meta]
Planner Preference = auto
```

### Prefer Paired
```
[Meta]
Planner Preference = prefer_paired
```

### Require Paired
```
[Meta]
Planner Preference = require_paired
```

- [ ] Confirm NO occurrence of:
```
Require Paired Plan
```

---

## 3. UI Save/Load Round Trip

For each option:
- [ ] Select option in UI
- [ ] Save
- [ ] Refresh page
- [ ] Confirm selection persists
- [ ] Confirm INI matches

---

## 4. Identity Behavior Tests

### Automatic Mode

| Callsign | Locator | Expected |
|----------|--------|---------|
| AA0NT | EM18 | Type1Single |
| AA0NT/12 | EM18 | Type2Single |
| <AA0NT> | EM18IG | Type3Single |
| AA0NT/12 | EM18IG | Type2Type3Paired |

- [ ] `<AA0NT> / EM18` rejected

---

### Prefer Paired Mode

- [ ] Type1 remains Type1
- [ ] Type3 remains Type3
- [ ] Paired-capable identities prefer paired
- [ ] AA0NT/12 + EM18IG → paired

---

### Require Paired Mode

- [ ] AA0NT + EM18 → rejected
- [ ] AA0NT/12 + EM18 → rejected (if not pairable)
- [ ] AA0NT/12 + EM18IG → paired

---

## 5. Runtime UI Verification

- [ ] Navbar shows correct plan type
- [ ] Frame progression visible (F1/2 → F2/2)
- [ ] Callsign/locator per frame correct

---

## 6. WebSocket / State

- [ ] plan_type correct
- [ ] frame_count correct
- [ ] current_frame increments
- [ ] frame_callsign correct
- [ ] frame_locator correct

---

## 7. CLI Tests

```
sudo ./build/bin/wsprrypi AA0NT EM18 20 80m
sudo ./build/bin/wsprrypi 'AA0NT/12' EM18 20 80m
sudo ./build/bin/wsprrypi '<AA0NT>' EM18IG 20 80m
```

- [ ] Behavior matches expectations

---

## 8. Restart Persistence

- [ ] Save preference
- [ ] Restart service/app
- [ ] Reload UI
- [ ] Confirm preference persists

---

## 9. Expected Log Output (Quick Validation)

### Type1Single
```
Selected WSPR plan: Type1Single
```

### Type2Single
```
Selected WSPR plan: Type2Single
```

### Type3Single
```
Selected WSPR plan: Type3Single
```

### Paired (Type2Type3Paired)
```
Selected WSPR plan: Type2Type3Paired
```

Frame progression example:
```
F1/2 AA0NT/12 EM18
F2/2 <AA0NT/12> EM18IG
```

### Prefer Paired Log Hint
```
Paired WSPR planning preferred when available.
```

---

## 10. Failure Signatures (What to Watch For)

### ❌ Planner Preference Not Persisting
Symptoms:
- UI resets to "Automatic" after refresh
- INI missing `[Meta]` section

Likely Cause:
- json_to_ini() not including Meta

---

### ❌ Legacy Field Reappears
Symptoms:
```
Require Paired Plan = true
```

Likely Cause:
- Old serialization path still active

---

### ❌ Auto Mode Rejects Valid Paired Case
Symptoms:
```
Plan status: InvalidLocator
```
for:
```
AA0NT/12 + EM18IG
```

Likely Cause:
- Planner not auto-upgrading to paired

---

### ❌ Require Paired Does Nothing
Symptoms:
- Type1 or Type2 still allowed in "Require paired"

Likely Cause:
- Preference not propagated to planner

---

### ❌ UI and Runtime Disagree
Symptoms:
- UI shows paired, logs show single
- Frame count mismatch

Likely Cause:
- Config vs runtime mismatch

---

### ❌ No Frame Progression
Symptoms:
- Only F1/2 shown
- Never reaches F2/2

Likely Cause:
- Scheduler/state machine issue

---

### ❌ Crash / Bad Alloc After Error
Symptoms:
```
std::bad_alloc
Aborted
```

Likely Cause:
- Exception handling regression

---

## 11. Final Sanity Pass

- [ ] Clean build passes
- [ ] All 3 modes behave correctly
- [ ] INI persists correctly
- [ ] UI round-trip works
- [ ] Logs match expectations
- [ ] No legacy fields remain
