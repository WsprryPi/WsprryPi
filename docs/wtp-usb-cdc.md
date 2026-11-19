# Pico USB roles and identity

External WTP execution is retired. There is no `backend=wtp`, Pi WTP listener, remote Fleet dispatch, Pico WTP TCP/TLS/USB/BLE carrier, or generic HTTP jobs API. No hidden downgrade or alternate job-submission path is supported. Existing WTP clients and Qualification Harness WTP campaigns are incompatible with this software; their adapters require separate work and are not replaced here.

Each device executes its own native schedule. See [native execution and output safety](native-execution-safety.md), [migration and recovery](wtp-status-recovery.md), and [Pico USB management](wtp-usb-management.md). Do not replace WTP-capable software until its deployment’s legacy authorities and cleanup duties are resolved.

Console is interface 0, Reserved is interface 2 (with data interface 3), Management is interface 4, and Logging is interface 6. The former WTP CDC allocation stays present and neutrally labeled. Application images drain its input with bounded servicing and provide no parser, response, or execution entry. The optional RFBench diagnostic image alone retains its independent, non-WTP Commands interface at that allocation.

Discover Management directly, without a WTP sibling or HELLO. Validate the physical USB parent, serial, VID/PID, role, full device identity, and boot/session binding. Existing explicit interface-2 locators may resolve interface 4 only under the same verified parent and identity. Ambiguous or missing roles refuse. Never probe Console or infer identity from tty numbering. USB permissions and physical access remain the host authority boundary.
