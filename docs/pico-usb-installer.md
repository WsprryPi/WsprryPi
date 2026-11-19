# Pico USB firmware access

The independent WsprryPico USB installer remains available. The former selected-backend browser firmware workflow is unavailable in retired software: `/api/control/v1/member/firmware` refuses without selecting a WTP runtime. Managing a Pico's native settings does not select a transmitter backend or authorize firmware installation.

Use the companion [standalone installation guide](https://wsprrypico.readthedocs.io/en/devel/install.html) and its [WsprryPi host service handoff](https://wsprrypico.readthedocs.io/en/devel/wsprrypi.html#standalone-installer-on-a-wsprrypi-host). Only one process may own the selected USB connection. Firmware programming, service operation, BOOTSEL, erasure, and physical recovery require explicit operator authorization and the applicable procedure.

## Retained installer contract

The shared core owns release selection, image and manifest verification, device identity, UF2 bounds, backup, programming, preservation, and recovery. The Pi retains its verified deployment and permission helpers, process/result validators, durable maintenance markers, and private transaction records. They do not create a replacement browser update workflow.

The main Pi installer provisions Python, libusb, USB permissions, and a compatible verified picotool package. Source deployment does not authorize running an installer. The independent core identifies a board by USB serial and physical parent, never by tty suffix or enumeration order. Console remains interface 0, Management interface 4, and Logging interface 6. Interfaces 2/3 are Reserved and inert in application images; RFBench retains its non-WTP Commands exception. The installer does not require a WTP sibling or negotiate a WTP session.

For paired management, require the current canonical control-schema digest from the selected source pair. The standalone core supports `--require-control-sha256` for this explicit check: missing or mismatched embedded metadata refuses before programming. An old published release is not automatically compatible with the current prerelease schema. Installing an image without a matched digest does not establish Pi management compatibility. Select and review an exact image and source; do not bypass a required mismatch or substitute another peer.

## Preservation and recovery

Preserve exact installer/image/schema identity, the full same-board backup, reserved flash, saved semantics, selected serial, physical port, and transaction records. A successful download or software image plan is not a completed installation. Verification must establish the new boot, device identity, firmware identity, and preserved state through the current independent management path.

Unknown or interrupted programming remains held. Use the original transaction's explicit recovery procedure and retained installer bytes; do not replace it with a new image or infer success from missing records. Connection-only recovery must not reflash the board. Full erase requires a separate explicit selection and complete blank-flash readback. Restoration requires the original complete image and saved semantics. Existing unresolved transaction files and maintenance markers must not be deleted as temporary residue.

Before replacing WTP-capable firmware, complete its outstanding execution, ownership, cleanup, and controller retirement duties with compatible software. See [migration and recovery](wtp-status-recovery.md). A current settings editor cannot perform legacy WTP cleanup.

## Software validation

The shared [installer engineering contract](https://github.com/WsprryPi/WsprryPico/blob/devel/engineering/development/usb-installer.md) owns its detailed transaction and preservation rules. Hardware-free tests cover its retained source and adapters:

```sh
python3 -B scripts/tests/pico_usb_deploy_test.py
python3 -B scripts/tests/pico_usb_bundle_test.py
```

Run the companion `tests/usb_installer_tests.py` and `tests/usb_tool_download_tests.py` against the intended Pico sources. These checks do not deploy tools, program a board, operate services, or qualify physical USB interruption recovery, GPIO, timing, or RF. Existing transaction evidence qualifies only its exact source, device, image, and procedure.
