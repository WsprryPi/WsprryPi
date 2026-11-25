#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Accept a provider candidate through public, non-installing interfaces."""
from __future__ import annotations
import argparse
from contextlib import nullcontext
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import rp1_gpclk_dkms_install as client
import rp1_drive_selection as drive_selection
require=client.require

REQUIRED_DRIVES = {2, 4, 8, 12}
DRIVE_BITS = {1: 2, 2: 4, 4: 8, 8: 12}
ROUTE_IDS = {"gpio4": 1, "gpio20": 2}
MANIFEST = "rp1-gpclk-dkms-installation-manifest-v1.json"
PROVENANCE = "release-metadata.json"


def source_identity(source):
    require(source.is_absolute() and source.resolve(strict=True)==source and source.is_dir(),'canonical provider source required')
    require(Path(client.run_git(source,'rev-parse','--show-toplevel')).resolve()==source,'provider source is not a repository root')
    client.canonical_origin(client.run_git(source,'remote','get-url','origin'))
    require(not client.run_git(source,'status','--porcelain=v1','--untracked-files=all'),'provider source is dirty')
    require(all(line.startswith('H ') for line in client.run_git(source,'ls-files','-v').splitlines()),'provider source has hidden index state')
    commit=client.run_git(source,'rev-parse','HEAD');tree=client.run_git(source,'rev-parse','HEAD^{tree}')
    require(client.SHA40.fullmatch(commit) and client.SHA40.fullmatch(tree),'provider Git identity malformed')
    return {'sourceCommit':commit,'sourceTree':tree}


def snapshot(candidate, destination=None):
    require(candidate.is_absolute() and candidate.resolve(strict=True)==candidate and candidate.is_dir(),'canonical candidate directory required')
    result={}
    for path in candidate.iterdir():
        require(len(result)<64,'oversized candidate inventory')
        data=read_asset(path, 64*1024*1024)
        result[path.name]=client.sha256_bytes(data)
        if destination is not None:
            (destination/path.name).write_bytes(data)
    require(bool(result),'candidate has no assets')
    return result


def read_asset(path, limit):
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError as error:
        raise client.ContractError(f"cannot read candidate asset {path.name}: {error.strerror}") from error
    with os.fdopen(descriptor, "rb") as stream:
        info = os.fstat(stream.fileno())
        require(stat.S_ISREG(info.st_mode) and info.st_size <= limit,
                f"unsafe or oversized candidate asset {path.name}")
        data = stream.read(limit + 1)
    require(len(data) == info.st_size, f"candidate asset changed while reading {path.name}")
    return data


def candidate_json(candidate, captured, name):
    require(name in captured, f"missing candidate metadata {name}")
    data = read_asset(candidate / name, client.MAX_API_BYTES)
    require(client.sha256_bytes(data) == captured[name], f"changed candidate metadata {name}")
    value = client.strict_json(data)
    require(isinstance(value, dict), f"invalid candidate metadata {name}")
    return value


def validate_drive_selection(candidate, captured, validation, identity, metadata, ui, cli):
    record = validation.get("driveCapabilities")
    require(isinstance(record, dict), "provider drive capability evidence is missing")
    require(set(record) == {"schemaVersion", "package", "uapiSha256", "routes"}
            and type(record["schemaVersion"]) is int and record["schemaVersion"] == 1,
            "provider drive capability record version or fields differ")
    manifest = candidate_json(candidate, captured, MANIFEST)
    provenance = candidate_json(candidate, captured, PROVENANCE)
    tag = "v" + metadata["productVersion"]
    require(manifest.get("sourceCommit") == identity["sourceCommit"]
            and all(provenance.get(k) == v for k, v in identity.items())
            and manifest.get("releaseTag") == provenance.get("releaseTag") == tag
            and all(manifest.get(k) == provenance.get(k) == metadata[k]
                    for k in ("productVersion", "debianVersion"))
            and provenance.get("installationManifestSha256") == captured[MANIFEST],
            "drive evidence provider source, tag, or manifest identity differs")
    package = record["package"]
    require(isinstance(package, dict) and set(package) == {"filename", "sha256"},
            "malformed drive evidence package identity")
    filename, digest = package["filename"], package["sha256"]
    require(isinstance(filename, str) and filename.endswith(".deb")
            and isinstance(digest, str) and client.SHA256.fullmatch(digest)
            and digest == captured.get(filename)
            and all(all(asset.get(k) == v for k, v in package.items())
                    for asset in (manifest["package"], provenance["package"])),
            "drive evidence package differs from the validated candidate")
    require(record["uapiSha256"] == manifest["uapi"]["sha256"]
            == provenance["uapi"]["rp1_gpclk.h"]["sha256"]
            == metadata["uapiSha256"]["consumer"],
            "drive evidence UAPI identity differs")

    routes = record["routes"]
    require(isinstance(routes, dict) and set(routes) == set(ROUTE_IDS),
            "drive evidence must contain GPIO4 and GPIO20 exactly once")
    problems = []
    for label, observed in (("Pi UI", ui), ("Pi CLI", cli)):
        if observed != REQUIRED_DRIVES:
            problems.append(drive_difference(label, observed))
    for route, route_id in ROUTE_IDS.items():
        value = routes[route]
        label = route.upper()
        require(isinstance(value, dict)
                and set(value) == {"routeId", "compatibilityId", "bootDtboSha256",
                                   "runtimeDtboSha256", "supportedDriveMaMask"},
                f"{label} drive evidence fields differ")
        route_metadata = provenance["routes"][route]
        require(type(value["routeId"]) is int and value["routeId"] == route_id == route_metadata["routeId"]
                and value["compatibilityId"] == route_metadata["compatibilityId"]
                == metadata["compatibilityIdentities"][route],
                f"{label} drive route identity differs")
        boot, runtime = value["bootDtboSha256"], value["runtimeDtboSha256"]
        entries = [item["sha256"] for item in manifest["packageInventory"]
                   if item.get("path") == route_metadata["packagePath"]]
        require(all(isinstance(h, str) and client.SHA256.fullmatch(h) for h in (boot, runtime))
                and boot == route_metadata["dtboSha256"] and entries == [boot] and boot != runtime,
                f"{label} drive overlay identity differs")
        mask = value["supportedDriveMaMask"]
        require(type(mask) is int and mask >= 0 and not mask & ~sum(DRIVE_BITS),
                f"{label} drive capability mask is invalid")
        observed = {drive for bit, drive in DRIVE_BITS.items() if mask & bit}
        if observed != REQUIRED_DRIVES:
            problems.append(drive_difference(label, observed))
    for key in ("bootDtboSha256", "runtimeDtboSha256"):
        require(routes["gpio4"][key] != routes["gpio20"][key], "drive overlay identities are substituted")
    require(not problems, "RP1 drive selection differs: " + "; ".join(problems))
    return {"schemaVersion": 1, "providerTag": tag, "package": package,
            "uapiSha256": record["uapiSha256"], "uiDriveMa": sorted(ui),
            "cliDriveMa": sorted(cli), "routes": routes}


def drive_difference(label, observed):
    differences = []
    for name, drives in (("missing", REQUIRED_DRIVES - observed), ("extra", observed - REQUIRED_DRIVES)):
        if drives:
            differences.append(f"{name} {', '.join(map(str, sorted(drives)))} mA")
    return label + " " + ", ".join(differences)


def accept(args):
    source=args.provider_source;identity=source_identity(source)
    entry=source/'scripts/provider_lifecycle.py'
    metadata=client.invoke(entry,'identity')['providerIdentity']
    client.validate_consumer_provider({'identity':metadata})
    if args.verify_publication:require('release-audit' in metadata['capabilities'],'provider lacks non-installing release audit')
    release=args.release or args.verify_publication
    consumer=drive_selection.consumer_identity(args.consumer_source) if release else None
    output=args.output
    require(output.is_absolute() and output.resolve()==output and not output.exists() and not output.is_symlink(),'new canonical output directory required')
    require(output.parent.is_dir(),'output parent is missing')
    probe=subprocess.run(['git','-C',str(output.parent),'rev-parse','--show-toplevel'],capture_output=True,check=False)
    require(probe.returncode!=0,'acceptance output must be outside Git')
    output.mkdir(mode=0o700)
    candidate=args.candidate
    if candidate is None:
        candidate=output/'release'
        command=[str(source/'scripts/build-release-candidate'),'--version',metadata['productVersion'],
                 '--source-commit',identity['sourceCommit'],'--output',str(candidate)]
        if release:command+=['--release']
        with (output/'build.log').open('wb') as log:
            subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
    drive_result=None
    publication=None
    candidate_context = tempfile.TemporaryDirectory(prefix="accepted-candidate-", dir=output) if release else nullcontext()
    with candidate_context as folder:
        checked_candidate = Path(folder) if release else candidate
        captured = snapshot(candidate, checked_candidate if release else None)
        command=[str(source/'scripts/validate-release-candidate'),'--candidate',str(checked_candidate),
                 '--source-commit',identity['sourceCommit']]
        if release:command+=['--release']
        checked=subprocess.run(command,text=True,capture_output=True,check=True,timeout=600)
        validation=client.strict_json(checked.stdout)
        require(isinstance(validation,dict) and validation.get('status')=='PASS'
                and validation.get('independentlyRebuilt') is True and validation.get('published') is False
                and validation.get('classification')==('release' if release else 'candidate')
                and all(validation.get(k)==v for k,v in identity.items()),'provider independent validation result differs')
        require(snapshot(checked_candidate)==captured,'candidate changed during independent validation')
        if args.compare_retained:require(snapshot(args.compare_retained)==captured,'retained candidate differs')
        if release:
            ui, cli, installer_tag, consumer_requirements=drive_selection.observe(
                args.consumer_source, consumer["sourceCommit"])
            require(consumer_requirements == client.strict_json(client.requirements_path().read_bytes()),
                    "selected WsprryPi source has different provider requirements")
            drive_result=validate_drive_selection(checked_candidate, captured, validation, identity, metadata, ui, cli)
            drive_result["consumerSource"]=consumer
        if args.verify_publication:
            require(drive_result["providerTag"] == installer_tag
                    and args.release_tag in (None, installer_tag),
                    "audited provider tag differs from the selected Pi installer's pin")
            state=output/'publication';state.mkdir(mode=0o700)
            requirements=output/'consumer-requirements.json'
            client.atomic_json(requirements,consumer_requirements)
            publication=client.invoke(entry,'verify-release','--state-dir',state,'--requirements',requirements,
                                      '--compare-candidate',checked_candidate,'--tag',installer_tag)
            require(snapshot(checked_candidate)==captured,'audited candidate changed during publication audit')
            require(publication.get('publicationVerified') is True and publication.get('installationPerformed') is False
                    and publication.get('candidateMatched') is True
                    and publication.get('releaseTag') == installer_tag
                    and publication.get('providerIdentity') == metadata
                    and all(publication.get(k)==v for k,v in identity.items()),'published provider differs from accepted source/candidate')
            client.validate_consumer_provider({'identity':publication.get('providerIdentity')})
    require(source_identity(source)==identity,'provider source changed during acceptance')
    if release:
        require(drive_selection.consumer_identity(args.consumer_source)==consumer,
                'WsprryPi consumer source changed during acceptance')
    require(snapshot(candidate)==captured,'candidate changed during acceptance')
    result={'schema':'wsprrypi-rp1-release-acceptance-v1','status':'PASS',**identity,
            'providerIdentity':metadata,'providerValidation':validation,'assets':captured,
            'productVersion':metadata['productVersion'],'debianVersion':metadata['debianVersion'],
            'acceptanceToolSha256':client.sha256_file(Path(__file__)),
            'consumerRequirementsSha256':client.sha256_file(client.requirements_path()),
            'publicationVerified':publication is not None,'productionSelectable':publication is not None,
            'published':publication is not None,'installationPerformed':False,'qualification':False}
    if release:
        result['rp1DriveSelection']=drive_result
    client.atomic_json(output/'acceptance.json',result)
    return result


def parser():
    result=argparse.ArgumentParser(description=__doc__)
    result.add_argument('--provider-source',type=Path,required=True)
    result.add_argument('--consumer-source',type=Path,default=ROOT,
                        help='clean WsprryPi source selected for release (default: this checkout)')
    result.add_argument('--output',type=Path,required=True,help='new external evidence directory')
    result.add_argument('--candidate',type=Path,help='existing assets; otherwise independently build them')
    result.add_argument('--release',action='store_true',help='require release-profile metadata; does not verify publication')
    result.add_argument('--compare-retained',type=Path)
    result.add_argument('--verify-publication',action='store_true',help='request public provider verification of published assets')
    result.add_argument('--release-tag',help='published tag, which must match the selected Pi installer pin')
    return result


def main(argv=None):
    try:
        args=parser().parse_args(argv)
        require(not args.release_tag or args.verify_publication,'release tag requires publication verification')
        print(json.dumps(accept(args),sort_keys=True));return 0
    except (OSError,ValueError,KeyError,TypeError,AttributeError,SyntaxError,
            client.ContractError,subprocess.SubprocessError) as error:
        print(json.dumps({'status':'FAIL','error':str(error)}),file=sys.stderr);return 1


if __name__=='__main__':raise SystemExit(main())
