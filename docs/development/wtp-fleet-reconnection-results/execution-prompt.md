# Fleet reconnection execution contract

Work in WsprryPi `devel`, starting at `3dd0a79894782b5dfd59fbd14ef6a282223eec6b`.
Preserve existing schedules, settings, profile data and consumed-slot history.
The operator authorizes the existing Pi service/configuration changes, 20m RF,
and GP/GPIO/PIO outputs. Complete this campaign without additional operator
questions or recabling. Eight-target capacity testing is excluded.

1. Use wspr5's installed central Fleet scheduler with the existing five targets:
   wspr1 GPIO4, wspr2 GPIO4, wspr4 GPIO4, Pico A GP2 and Pico B GP2. Keep wspr5
   local Enable off. Record actual full identities, boots, source/binary hashes
   and saved schedule definitions before work.
2. Run simultaneous finite ten-second tones every two minutes on the saved
   distinct 20m frequencies. Observe target state independently of controller
   status. Use the wspr5 SDR to confirm corresponding signals from the currently
   connected wspr2/Pico A outputs. RF-chain inventory, calibrated frequency,
   decode and precision edge timing are outside this acceptance criterion.
3. Exercise controller-wide connectivity loss, two target subsets, controller
   process crash with managed restart, and target-service restart during work.
   Hold network interruptions beyond the maximum ownership lease. Physical
   discovery interfaces are unchanged; these outages do not qualify DNS-SD
   topology recovery.
4. Require explicit Reconcile, fresh expected identity and unowned/inactive
   status before Resume. Confirm all five complete subsequent scheduled rounds,
   unaffected targets continue, consumed slots never go backwards, and no
   duplicate job or replayed consumed slot is observed.
5. Stop on observer failure, retain failed attempts separately, and install an
   independent bounded cleanup guard. Pause all assignments, remove only this
   campaign's temporary routes and reconcile output state on every exit.
6. Repair a demonstrated application defect narrowly, retain ownership/local
   priority/protocol-fault boundaries, add meaningful focused rejection tests,
   run applicable Mac/Linux suites and repeat the affected physical case.
   Review the actual Reconcile workflow with Impeccable at desktop/mobile sizes.
7. Restore any temporarily selected Pico image, verify reserved provisioning
   bytes and saved state are unchanged, and independently inspect all six LAN
   endpoints. Finish with canonical managed services, all five assignments
   paused, no in-flight barrier, local Enable off, inactive/unowned outputs,
   original network routes and no campaign timers or receiver processes.
8. Record compact evidence in this repository; keep large private observations
   and raw IQ outside Git. Compress large transfers with tar piped through gzip.
   Perform adversarial review, repair findings, reassess, commit and push devel,
   verify remote parity, and report passes and the exact remaining test scope.
