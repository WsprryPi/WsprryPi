#!/usr/bin/env python3
"""Exercise the actual provider/installer JSON boundary without hardware.

Pass an explicit RP1-GPCLK-DKMS checkout. Only its offline test adapters run.
"""
import contextlib
import importlib.util
import io
import json
import subprocess
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

APP = Path(__file__).resolve().parents[2]
if len(sys.argv) != 2:
    raise SystemExit("usage: rp1_gpclk_update_contract_test.py /path/to/RP1-GPCLK-DKMS")
PROVIDER = Path(sys.argv.pop()).resolve(strict=True)
sys.path[:0] = [str(PROVIDER / "scripts"), str(PROVIDER / "tests")]
import runtime_provider as provider
from check_runtime_provider import UpdateHost

spec = importlib.util.spec_from_file_location("wsprrypi_installer", APP / "scripts/rp1_gpclk_dkms_install.py")
installer = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = installer
spec.loader.exec_module(installer)


class PublicRunner:
    def __init__(self, host):
        self.host = host
        self.calls = []

    def run(self, command, **unused):
        self.calls.append(command[2])
        if command[:2] != ["python3", str(PROVIDER / "scripts/runtime_provider.py")]:
            raise AssertionError("unexpected operational command")
        output = io.StringIO()
        with mock.patch.object(sys, "argv", command[1:]), contextlib.redirect_stdout(output):
            result = provider.main(self.host)
        return installer.CommandResult(output.getvalue(), "", result)


class ContractTests(unittest.TestCase):
    def test_generated_provider_bundle_passes_installer_validation(self):
        import build_runtime_binding as builder
        import build_runtime_bundle as bundler
        from check_runtime_provider import KERNEL
        # Synthetic module images/notes and a mapped companion keep this
        # hardware-free. Build real bindings, payloads and bootstrap helpers.
        bundler.generate(PROVIDER / 'build/runtime-controller')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            modules = root / 'lib/modules' / KERNEL / 'updates/dkms'
            modules.mkdir(parents=True)
            prefix = (b'\x7fELF\x02\x01\0version=' + builder.PRODUCT_VERSION.encode() +
                      b'\0vermagic=' + KERNEL.encode() + b' ')
            consumer = prefix + b'\0rp1_runtime_controller=1\0rp1_route_controller\0'
            controller = prefix + b''.join((PROVIDER / 'build/runtime-controller' /
                (route + '.dtbo')).read_bytes() for route in ('gpio4', 'gpio20'))
            (modules / 'rp1_gpclk_dkms.ko').write_bytes(consumer)
            (modules / 'rp1_route_controller.ko').write_bytes(controller)
            companion = root / 'companion.py'
            companion.write_bytes((APP / 'scripts/route_application.py').read_bytes())
            output = root / 'bundle'
            with mock.patch.object(builder, 'module_note', return_value=b'offline ELF note'):
                bound = bundler.bundle(modules, output, companion, KERNEL)
            installed_companion = mock.MagicMock(spec=Path)
            installed_companion.__str__.return_value = builder.APPLICATION
            installed_companion.is_file.side_effect = companion.is_file
            installed_companion.is_symlink.side_effect = companion.is_symlink
            installed_companion.open.side_effect = companion.open
            resolved = {'channel': 'development', 'commit': bound['sourceCommit'],
                        'version': bound['productVersion']}
            with mock.patch.object(installer.platform, 'release', return_value=KERNEL):
                checked = installer.validate_runtime_bundle(output, resolved,
                    companion=installed_companion, module_root=root)
                self.assertEqual(checked['binding'], bound)
                manager = output / 'runtime_manager.py'
                original = manager.read_bytes()
                manager.unlink()
                with self.assertRaisesRegex(installer.ContractError, 'unsupported or missing member'):
                    installer.validate_runtime_bundle(output, resolved,
                        companion=installed_companion, module_root=root)
                manager.write_bytes(original + b'\n# changed helper\n')
                with self.assertRaisesRegex(installer.ContractError, 'bootstrap digest differs'):
                    installer.validate_runtime_bundle(output, resolved,
                        companion=installed_companion, module_root=root)
                manager.write_bytes(original)
            helpers = sorted(path.stem for path in output.glob('runtime_*.py'))
            subprocess.run([sys.executable, '-I', '-c',
                'import importlib,pathlib,sys; root=pathlib.Path(sys.argv[1]); '
                'sys.path.insert(0,str(root)); '
                'modules=[importlib.import_module(name) for name in sys.argv[2:]]; '
                'assert all(pathlib.Path(module.__file__).parent == root for module in modules)',
                str(output), *helpers], cwd=root, check=True)

    def test_real_json_contract_and_lifecycle_planners(self):
        for route in (None, 4, 20, "removed"):
            for active, masked in ((True, False), (False, False), (False, True)):
                if route in (4, 20) and not active:
                    continue
                with self.subTest(route=route, active=active, masked=masked):
                    host = UpdateHost(active, masked, removed=route == "removed")
                    receipt = provider.installation_status(host)["receipt"]
                    if route in (4, 20):
                        host.select_route(route)
                    record = {"schema": installer.RUNTIME_RECORD_SCHEMA, "channel": "development",
                              "sourceCommit": host.value["sourceCommit"], "productVersion": receipt["productVersion"],
                              "runtime": receipt}
                    runner = PublicRunner(host)
                    with tempfile.TemporaryDirectory() as tmp, \
                         mock.patch.object(installer, "RUNTIME_PROVIDER", PROVIDER / "scripts/runtime_provider.py"), \
                         mock.patch.object(installer.platform, "release", return_value=host.running_kernel()), \
                         mock.patch.object(installer, "require_unchanged_ownership"):
                        installer.recover_and_remove_owned_runtime(Path(tmp) / "record.json", record, object(), runner)
                    self.assertTrue(host.absent)
                    self.assertIn("update-execute", runner.calls)
                    self.assertTrue(set(runner.calls) <= {"capabilities", "update-plan", "update-execute"})

    def test_installer_cannot_override_provider_identity_or_unsafe_state(self):
        for bad in ("identity", "owner", "artifact"):
            host = UpdateHost()
            receipt = provider.installation_status(host)["receipt"]
            if bad == "identity": receipt["bindingSha256"] = "0" * 64
            elif bad == "owner": host.system.controller_open = True
            else: host._artifacts[next(iter(host._artifacts))]["status"] = "changed"
            record = {"schema": installer.RUNTIME_RECORD_SCHEMA, "channel": "development",
                      "sourceCommit": host.value["sourceCommit"], "productVersion": receipt["productVersion"], "runtime": receipt}
            runner = PublicRunner(host)
            with mock.patch.object(installer, "RUNTIME_PROVIDER", PROVIDER / "scripts/runtime_provider.py"), \
                 mock.patch.object(installer.platform, "release", return_value=host.running_kernel()), \
                 mock.patch.object(installer, "require_unchanged_ownership"):
                with self.assertRaises(ValueError):
                    installer.recover_and_remove_owned_runtime(Path("/offline"), record, object(), runner)
            self.assertNotIn("update-execute", runner.calls)
            self.assertEqual(host.effects, [])


if __name__ == "__main__":
    unittest.main()
