from pathlib import Path
import hashlib,json,subprocess,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[3]
out=Path(__file__).resolve().parent
old=root/'docs/qualification/m2.5-d8-b4-20261016-4'
report=root/'docs/m2.5-d8-b4-audit.md'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def dump(name,data): (out/name).write_text(json.dumps(data,indent=2)+'\n')
def git(*args): return subprocess.check_output(['git',*args],cwd=root,text=True)
def counts(path):
    result={'PASS':0,'FAIL':0,'SKIP':0,'NOT EXECUTED':0}
    for t in ET.parse(path).getroot().iter('testcase'):
        status='FAIL' if t.find('failure') is not None else 'SKIP' if t.find('skipped') is not None else 'PASS'
        result[status]+=1
    return result
results=json.loads((out/'clean/results.json').read_text())
assert set(results)=={'Debug','Release'}
for v in results.values(): assert v['counts']['FAIL']==0
for name in ['supplement-results.json','boundaries-results.json']:
    data=json.loads((out/name).read_text());assert set(data)=={'Debug','Release'}
    assert all(s=='PASS' for config in data.values() for s in config.values())
extra={n:counts(out/n) for n in ['sanitizer-junit.xml','btrfs-Debug-junit.xml','btrfs-Release-junit.xml']}
assert all(v=={'PASS':6,'FAIL':0,'SKIP':1,'NOT EXECUTED':0} for v in extra.values())
dump('verified-results.json',{'clean':{k:v['counts'] for k,v in results.items()},'targeted':extra,'supplemental':'Both consumers PASS in both configurations on tmpfs and Btrfs; no CTest registration/count inferred from consumer checks'})
for line in (out/'clean/SOURCE.sha256').read_text().splitlines():
    digest,name=line.split('  ',1);assert sha(root/name)==digest,name
fingerprints=json.loads((out/'prior-fingerprints.json').read_text())
assert all((root/n).is_file() and sha(root/n)==d for n,d in fingerprints.items())
prior_dirs={str(Path(n).parent) for n in fingerprints if '/qualification/' in n}
# Verify the complete old tree file sets, not only the originally known paths.
for dirname in ['m2.5-d8-b1-toml-20261015-1','m2.5-d8-b4-gate-20261016-1',*[f'm2.5-d8-b4-20261016-{i}' for i in range(1,5)]]:
    parent=root/'docs/qualification'/dirname
    actual={str(p.relative_to(root)) for p in parent.rglob('*') if p.is_file()}
    expected={n for n in fingerprints if n.startswith(str(parent.relative_to(root))+'/')}
    assert actual==expected,dirname
protected=[n for n in fingerprints if n=='docs/m2.5-d8b-cli-integration.md' or n.startswith('docs/qualification/m2.5-d8-b1-toml-20261015-1/')]
dump('historical-preservation.json',{'result':'PASS','protected_files':len(protected),'all_previous_evidence_and_protected_files':len(fingerprints),'complete_prior_file_sets_unchanged':True,'fingerprint_inventory_sha256':sha(out/'prior-fingerprints.json')})
base=[line.split('  ',1)[1] for line in (old/'CANDIDATE.sha256').read_text().splitlines()]+['docs/m2.5-d8-b4-audit.md']
assert len(base)==12 and len(set(base))==12
for name in base:
    assert not (root/name).is_symlink() and (root/name).is_file()
assert sha(root/'src/generation/publication.cpp')==sha(out/'baseline/src/generation/publication.cpp')
assert git('rev-parse','HEAD').strip()=='c0a8b12528c979fbaba1be86d36629c3122c20c2'
assert git('branch','--show-current').strip()=='main'
assert subprocess.run(['git','diff','--cached','--quiet'],cwd=root).returncode==0
check=subprocess.run(['git','diff','--check'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
assert check.returncode==0; (out/'git-diff-check.txt').write_text(check.stdout)
for n in base:
    for lineno,line in enumerate((root/n).read_bytes().splitlines(),1):
        assert line==line.rstrip(b' \t'),(n,lineno)
assert not list(root.glob('.b4-btrfs-*')),'unremoved physical witness parent'
# Create final names before the exact Git and commit-path snapshots.
for n in ['COMMIT-FILES.txt','COMMIT.sha256','ARTIFACTS.sha256','FILES.txt','candidate-inventory.json','git-status-final.txt','git-status-untracked-all.txt','git-index.txt','git-readiness.json','evidence-whitespace.json','COMMIT-PROPOSAL.txt']:
    (out/n).touch(exist_ok=True)
(out/'COMMIT-PROPOSAL.txt').write_text('feat(activation): integrate and qualify D8-B4 package operations\n\nAdd internal install/remove preparation, D4 retention and B1/B2/B3 activation routing.\nPreserve dependency closure and generation/store lifetime; support safe Btrfs GC discovery.\nInclude independent audit regressions, clean Debug/Release qualification and targeted sanitizer/filesystem evidence.\nNo staging or commit was performed.\n')
(out/'git-status-final.txt').write_text(git('status','--short'))
(out/'git-status-untracked-all.txt').write_text(git('status','--short','--untracked-files=all'))
(out/'git-index.txt').write_text(git('diff','--cached','--raw'))
files=sorted(set(base+[str(p.relative_to(root)) for parent in [old,out] for p in parent.rglob('*') if p.is_file()]))
assert not any(n in protected or n.startswith('docs/qualification/m2.5-d8-b1-toml-20261015-1/') for n in files)
assert not any((root/n).is_symlink() or (root/n).suffix in ['.tmp','.o','.so','.pyc'] for n in files)
ignored=subprocess.run(['git','check-ignore','--stdin'],cwd=root,input='\n'.join(files)+'\n',text=True,stdout=subprocess.PIPE)
assert ignored.returncode==1,(ignored.returncode,ignored.stdout)
(out/'COMMIT-FILES.txt').write_text(''.join(n+'\n' for n in files))
(out/'FILES.txt').write_text(''.join(str(p.relative_to(out))+'\n' for p in sorted(out.rglob('*')) if p.is_file()))
whitespace=[]
for n in files:
    if n in base:continue
    data=(root/n).read_bytes()
    try:text=data.decode('utf-8')
    except UnicodeDecodeError: continue
    lines=[i for i,l in enumerate(text.splitlines(),1) if l!=l.rstrip(' \t')]
    if lines or '\r' in text or (text and not text.endswith('\n')):
        whitespace.append({'file':n,'trailing_space_lines':lines,'carriage_returns':text.count('\r'),'missing_final_lf':bool(text and not text.endswith('\n')),'classification':'intentional verbatim qualification evidence; preserved bytes, excluded from editable-source whitespace PASS'})
dump('evidence-whitespace.json',whitespace)
status=git('status','--porcelain=v1','--untracked-files=all').splitlines()
excluded=[s[3:] for s in status if s[3:] not in files]
dump('git-readiness.json',{'head':git('rev-parse','HEAD').strip(),'branch':'main','index':'EMPTY DIFF AGAINST HEAD; no staging performed','git_diff_check':'PASS','editable_candidate_whitespace':'PASS','intentional_evidence_whitespace_files':len(whitespace),'implementation_and_report_files':len(base),'prior_final_evidence_files':sum(1 for p in old.rglob('*') if p.is_file()),'new_audit_evidence_files':sum(1 for p in out.rglob('*') if p.is_file()),'total_candidate_files':len(files),'excluded_worktree_files':excluded,'temporary_files_in_candidate':False,'btrfs_witness_parents_left':False})
marker='<!-- AUDIT FINAL RESULTS -->'
s=report.read_text().split(marker)[0].rstrip()+'\n\n'+marker+'\n\n'
s+='| Run | PASS | FAIL | SKIP | NOT EXECUTED |\n| --- | ---: | ---: | ---: | ---: |\n'
for config in ['Debug','Release']:
    c=results[config]['counts'];s+=f'| Clean {config} | {c["PASS"]} | {c["FAIL"]} | {c["SKIP"]} | {c["NOT EXECUTED"]} |\n'
for name,c in extra.items():s+=f'| {name} | {c["PASS"]} | {c["FAIL"]} | {c["SKIP"]} | outside targeted scope |\n'
s+=f'''\nBoth supplemental consumers PASS in Debug/Release on tmpfs/Btrfs. Their individual
scenario checks are not added to the registered CTest counts. All Btrfs witness
parents were removed empty. The unchanged `store_metadata_errors_test.cpp`
produces an existing nodiscard warning during clean builds; no build failed.

The final 407-input `clean/SOURCE.sha256` seal is
`{sha(out/'clean/SOURCE.sha256')}`. All compiled input bytes were rechecked.
Prior final evidence: all 71 entries in its 72-file bundle verify unchanged;
all 11 original candidate hashes and all 407 old source inputs verify against
unchanged live files or the preserved audit-entry snapshots in `baseline/`.
Old source manifests describe the old tested revision, not the corrected tests.
`prior-integrity-verification.json` makes that distinction explicit.

The proposed commit contains **{len(files)} files**: 12 implementation/report
files, 72 previous final qualification files and {sum(1 for p in out.rglob('*') if p.is_file())}
new audit evidence files. `COMMIT-FILES.txt` lists every repository-relative path,
including the seal files. `candidate-inventory.json` inventories the 12 editable
implementation/report files with their exact SHA-256 and sizes. `COMMIT.sha256`
hashes every proposed file except itself and `ARTIFACTS.sha256`, avoiding a hash
cycle. `ARTIFACTS.sha256` then seals every audit file except itself, including
`COMMIT.sha256`. Its outer checksum is delivered with the final response.
Recompute the commit manifest from the repository root and each evidence seal
from its own directory using `sha256sum -c`. `seal-audit.py` records the generation
and validation procedure; successful execution does not stage anything.

All {len(protected)} protected historical files and all previous qualification trees are
byte-for-byte unchanged, with complete file-set checks. Protected paths, the
exploratory B4 gate and B4 runs 1–3 remain in the workspace but are explicitly
excluded from the commit inventory; they are preserved diagnostic evidence.
The index has no difference against HEAD. All candidate source/report whitespace
checks and `git diff --check` PASS. Exact status, index and excluded-file lists
are attached. Evidence whitespace is separately classified in
`evidence-whitespace.json`: verbatim logs/snapshots/diffs are not normalized or silently
counted as editable-source whitespace failures. No temporary binaries, build
folders, Python caches or physical witness parents enter the candidate.

Proposed commit: `feat(activation): integrate and qualify D8-B4 package operations`.
The body is in `COMMIT-PROPOSAL.txt`. No git add, commit, push, reset or checkout;
no sudo, mount, privileged execution, stress OOM, B5/B6 or legacy CLI changes.

**APPROVE**, for the reviewed internal APIs and verified nonprivileged perimeter.
No open CRITICAL/HIGH/MEDIUM finding remains there. The SKIP and NOT EXECUTED
physical/deployment cases above remain explicit limitations of the qualification.
'''
report.write_text(s)
dump('candidate-inventory.json',[{'path':n,'sha256':sha(root/n),'bytes':(root/n).stat().st_size,'git_state':('tracked modified' if subprocess.run(['git','ls-files','--error-unmatch',n],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0 else 'untracked')} for n in sorted(base)])
(out/'COMMIT.sha256').write_text(''.join(sha(root/n)+'  '+n+'\n' for n in files if n not in [str((out/'COMMIT.sha256').relative_to(root)),str((out/'ARTIFACTS.sha256').relative_to(root))]))
(out/'ARTIFACTS.sha256').write_text(''.join(sha(p)+'  '+str(p.relative_to(out))+'\n' for p in sorted(out.rglob('*')) if p.is_file() and p!=out/'ARTIFACTS.sha256'))
# Read-only final verification of every seal after all writes.
for manifest,basepath in [(out/'COMMIT.sha256',root),(out/'ARTIFACTS.sha256',out),(old/'ARTIFACTS.sha256',old)]:
    for line in manifest.read_text().splitlines():
        digest,name=line.split('  ',1);assert sha(basepath/name)==digest,(manifest,name)
print(json.dumps({'candidate_files':len(files),'source_manifest_sha256':sha(out/'clean/SOURCE.sha256'),'commit_manifest_sha256':sha(out/'COMMIT.sha256'),'outer_artifacts_sha256':sha(out/'ARTIFACTS.sha256'),'status':'APPROVE'},indent=2))
