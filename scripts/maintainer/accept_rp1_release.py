#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Accept a provider candidate through public, non-installing interfaces."""
from __future__ import annotations
import argparse
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
require=client.require


def source_identity(source):
    require(source.is_absolute() and source.resolve(strict=True)==source and source.is_dir(),'canonical provider source required')
    require(Path(client.run_git(source,'rev-parse','--show-toplevel')).resolve()==source,'provider source is not a repository root')
    client.canonical_origin(client.run_git(source,'remote','get-url','origin'))
    require(not client.run_git(source,'status','--porcelain=v1','--untracked-files=all'),'provider source is dirty')
    require(all(line.startswith('H ') for line in client.run_git(source,'ls-files','-v').splitlines()),'provider source has hidden index state')
    commit=client.run_git(source,'rev-parse','HEAD');tree=client.run_git(source,'rev-parse','HEAD^{tree}')
    require(client.SHA40.fullmatch(commit) and client.SHA40.fullmatch(tree),'provider Git identity malformed')
    return {'sourceCommit':commit,'sourceTree':tree}


def snapshot(candidate):
    require(candidate.is_absolute() and candidate.resolve(strict=True)==candidate and candidate.is_dir(),'canonical candidate directory required')
    result={}
    for path in candidate.iterdir():
        info=path.lstat()
        require(stat.S_ISREG(info.st_mode) and info.st_size<=64*1024*1024 and len(result)<64,'unsafe or oversized candidate inventory')
        result[path.name]=client.sha256_file(path)
    require(bool(result),'candidate has no assets')
    return result


def accept(args):
    source=args.provider_source;identity=source_identity(source)
    entry=source/'scripts/provider_lifecycle.py'
    metadata=client.invoke(entry,'identity')['providerIdentity']
    client.validate_consumer_provider({'identity':metadata})
    if args.verify_publication:require('release-audit' in metadata['capabilities'],'provider lacks non-installing release audit')
    output=args.output
    require(output.is_absolute() and output.resolve()==output and not output.exists() and not output.is_symlink(),'new canonical output directory required')
    require(output.parent.is_dir(),'output parent is missing')
    probe=subprocess.run(['git','-C',str(output.parent),'rev-parse','--show-toplevel'],capture_output=True,check=False)
    require(probe.returncode!=0,'acceptance output must be outside Git')
    output.mkdir(mode=0o700)
    release=args.release or args.verify_publication
    candidate=args.candidate
    if candidate is None:
        candidate=output/'release'
        command=[str(source/'scripts/build-release-candidate'),'--version',metadata['productVersion'],
                 '--source-commit',identity['sourceCommit'],'--output',str(candidate)]
        if release:command+=['--release']
        with (output/'build.log').open('wb') as log:
            subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
    captured=snapshot(candidate)
    command=[str(source/'scripts/validate-release-candidate'),'--candidate',str(candidate),
             '--source-commit',identity['sourceCommit']]
    if release:command+=['--release']
    checked=subprocess.run(command,text=True,capture_output=True,check=True,timeout=600)
    validation=client.strict_json(checked.stdout)
    require(isinstance(validation,dict) and validation.get('status')=='PASS'
            and validation.get('independentlyRebuilt') is True and validation.get('published') is False
            and validation.get('classification')==('release' if release else 'candidate')
            and all(validation.get(k)==v for k,v in identity.items()),'provider independent validation result differs')
    require(snapshot(candidate)==captured,'candidate changed during acceptance')
    if args.compare_retained:require(snapshot(args.compare_retained)==captured,'retained candidate differs')
    publication=None
    if args.verify_publication:
        state=output/'publication';state.mkdir(mode=0o700)
        requirements=output/'consumer-requirements.json'
        client.atomic_json(requirements,client.strict_json(client.requirements_path().read_bytes()))
        arguments=['--state-dir',state,'--requirements',requirements,'--compare-candidate',candidate]
        if args.release_tag:arguments+=['--tag',args.release_tag]
        publication=client.invoke(entry,'verify-release',*arguments)
        require(publication.get('publicationVerified') is True and publication.get('installationPerformed') is False
                and publication.get('candidateMatched') is True
                and all(publication.get(k)==v for k,v in identity.items()),'published provider differs from accepted source/candidate')
        client.validate_consumer_provider({'identity':publication.get('providerIdentity')})
    require(source_identity(source)==identity,'provider source changed during acceptance')
    require(snapshot(candidate)==captured,'candidate changed during acceptance')
    result={'schema':'wsprrypi-rp1-release-acceptance-v1','status':'PASS',**identity,
            'providerIdentity':metadata,'providerValidation':validation,'assets':captured,
            'productVersion':metadata['productVersion'],'debianVersion':metadata['debianVersion'],
            'acceptanceToolSha256':client.sha256_file(Path(__file__)),
            'consumerRequirementsSha256':client.sha256_file(client.requirements_path()),
            'publicationVerified':publication is not None,'productionSelectable':publication is not None,
            'published':publication is not None,'installationPerformed':False,'qualification':False}
    client.atomic_json(output/'acceptance.json',result)
    return result


def parser():
    result=argparse.ArgumentParser(description=__doc__)
    result.add_argument('--provider-source',type=Path,required=True)
    result.add_argument('--output',type=Path,required=True,help='new external evidence directory')
    result.add_argument('--candidate',type=Path,help='existing assets; otherwise independently build them')
    result.add_argument('--release',action='store_true',help='require release-profile metadata; does not verify publication')
    result.add_argument('--compare-retained',type=Path)
    result.add_argument('--verify-publication',action='store_true',help='request public provider verification of published assets')
    result.add_argument('--release-tag',help='specific published tag; otherwise provider selects latest eligible release')
    return result


def main(argv=None):
    try:
        args=parser().parse_args(argv)
        require(not args.release_tag or args.verify_publication,'release tag requires publication verification')
        print(json.dumps(accept(args),sort_keys=True));return 0
    except (OSError,ValueError,KeyError,TypeError,client.ContractError,subprocess.SubprocessError) as error:
        print(json.dumps({'status':'FAIL','error':str(error)}),file=sys.stderr);return 1


if __name__=='__main__':raise SystemExit(main())
