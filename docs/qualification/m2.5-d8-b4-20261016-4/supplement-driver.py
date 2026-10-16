from pathlib import Path
import subprocess,os,json,time,tempfile,hashlib
root=Path('/home/alessandro/Lab/Programming/athena-pm');out=root/'docs/qualification/m2.5-d8-b4-20261016-4'
summary={}
for config in ['Debug','Release']:
    deadline=time.monotonic()+2400
    while not (out/f'{config}-inventory.json').exists():
        if time.monotonic()>deadline:raise SystemExit('main qualification build did not finish')
        time.sleep(2)
    print('RUN supplemental',config,flush=True)
    build=Path('/tmp/athena-b4-build-20261016-4')/config.lower();binary=Path(f'/tmp/athena-b4-mixed-{config}-20261016-4')
    command=['c++','-std=c++20',*(['-g','-O0'] if config=='Debug' else ['-O3','-DNDEBUG']),'-UNDEBUG','-I'+str(root/'tests'),'-I'+str(root/'src'),'-I'+str(root/'include'),'-I'+str(root/'third_party'),str(out/'mixed-concurrency.cpp'),'-L'+str(build),'-Wl,-rpath,'+str(build),'-lathena_core','-o',str(binary)]
    commands=[command,[str(binary)]];summary[config]={}
    for name,cmd in zip(['build','tests'],commands):
        with (out/f'mixed-{config}-{name}.txt').open('w') as log:rc=subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT).returncode
        summary[config][f'mixed-{name}']='PASS' if rc==0 else 'FAIL'
        if rc:raise SystemExit(rc)
    (out/f'mixed-{config}-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    (out/f'mixed-{config}-BUILD.sha256').write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+'  '+binary.name+'\n')
    parent=Path(tempfile.mkdtemp(prefix='.b4-btrfs-qualified-',dir=root));env=os.environ.copy();env['ATHENA_EXECUTION_TEST_TMPDIR']=str(parent);env['TMPDIR']=str(parent)
    pattern='^(package_operation_test|package_operation_scanner_test|generation_operation_test|operational_request_archive_test|operational_request_runtime_test|operational_activation_test|transaction_gc_protocol_test)$'
    cmd=['ctest','--test-dir',str(build),'-R',pattern,'--output-on-failure','--output-junit',str(out/f'btrfs-{config}-junit.xml')]
    info={'command':cmd,'fixture_parent':str(parent),'environment':{k:env[k] for k in ['ATHENA_EXECUTION_TEST_TMPDIR','TMPDIR']},'filesystem':subprocess.check_output(['stat','-f','-c','%T',str(parent)],text=True).strip()}
    with (out/f'btrfs-{config}-tests.txt').open('w') as log:rc=subprocess.run(cmd,env=env,cwd=root,stdout=log,stderr=subprocess.STDOUT).returncode
    summary[config]['btrfs-suite']='PASS' if rc==0 else 'FAIL';info['ctest_returncode']=rc
    with (out/f'btrfs-mixed-{config}-tests.txt').open('w') as log:mixed_rc=subprocess.run([str(binary)],env=env,cwd=root,stdout=log,stderr=subprocess.STDOUT).returncode
    info['supplement_command']=[str(binary)];info['supplement_returncode']=mixed_rc
    summary[config]['btrfs-mixed']='PASS' if mixed_rc==0 else 'FAIL'
    try:parent.rmdir();info['cleanup']='removed empty witness parent'
    except OSError as e:info['cleanup']=str(e)
    (out/f'btrfs-{config}-command.json').write_text(json.dumps(info,indent=2)+'\n')
    (out/'supplement-results.json').write_text(json.dumps(summary,indent=2)+'\n')
    if rc or mixed_rc:raise SystemExit(rc or mixed_rc)
print(json.dumps(summary,indent=2),flush=True)
