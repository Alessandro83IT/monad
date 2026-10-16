from pathlib import Path
import subprocess,os,json,time,tempfile,hashlib
root=Path('/home/alessandro/Lab/Programming/athena-pm');out=root/'docs/qualification/m2.5-d8-b4-audit-20261016-1'
summary={}
for config in ['Debug','Release']:
    deadline=time.monotonic()+2400
    while not (out/'clean'/f'{config}-inventory.json').exists():
        if time.monotonic()>deadline:raise SystemExit('main qualification build did not finish')
        time.sleep(2)
    print('RUN supplemental',config,flush=True)
    build=Path('/tmp/athena-b4-audit-build-20261016-1')/config.lower();binary=Path(f'/tmp/athena-b4-audit-boundaries-{config}-20261016-1')
    command=['c++','-std=c++20',*(['-g','-O0'] if config=='Debug' else ['-O3','-DNDEBUG']),'-UNDEBUG','-I'+str(root/'tests'),'-I'+str(root/'src'),'-I'+str(root/'include'),'-I'+str(root/'third_party'),str(out/'audit-boundaries.cpp'),'-L'+str(build),'-Wl,-rpath,'+str(build),'-lathena_core','-o',str(binary)]
    commands=[command,[str(binary)]];summary[config]={}
    for name,cmd in zip(['build','tests'],commands):
        with (out/f'boundaries-{config}-{name}.txt').open('w') as log:rc=subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT).returncode
        summary[config][f'boundaries-{name}']='PASS' if rc==0 else 'FAIL'
        if rc:raise SystemExit(rc)
    (out/f'boundaries-{config}-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    (out/f'boundaries-{config}-BUILD.sha256').write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+'  '+binary.name+'\n')
    parent=Path(tempfile.mkdtemp(prefix='.b4-btrfs-audit-boundaries-',dir=root));env=os.environ.copy();env['ATHENA_EXECUTION_TEST_TMPDIR']=str(parent);env['TMPDIR']=str(parent)
    info={'fixture_parent':str(parent),'environment':{k:env[k] for k in ['ATHENA_EXECUTION_TEST_TMPDIR','TMPDIR']},'filesystem':subprocess.check_output(['stat','-f','-c','%T',str(parent)],text=True).strip()}
    with (out/f'btrfs-boundaries-{config}-tests.txt').open('w') as log:mixed_rc=subprocess.run([str(binary)],env=env,cwd=root,stdout=log,stderr=subprocess.STDOUT).returncode
    info['supplement_command']=[str(binary)];info['supplement_returncode']=mixed_rc
    summary[config]['btrfs-boundaries']='PASS' if mixed_rc==0 else 'FAIL'
    try:parent.rmdir();info['cleanup']='removed empty witness parent'
    except OSError as e:info['cleanup']=str(e)
    (out/f'boundaries-btrfs-{config}-command.json').write_text(json.dumps(info,indent=2)+'\n')
    (out/'boundaries-results.json').write_text(json.dumps(summary,indent=2)+'\n')
    if mixed_rc:raise SystemExit(mixed_rc)
print(json.dumps(summary,indent=2),flush=True)
