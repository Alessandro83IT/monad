from pathlib import Path
import subprocess,json,os,hashlib,shutil
root=Path('/home/alessandro/Lab/Programming/athena-pm');out=root/'docs/qualification/m2.5-d8-b4-audit-20261016-1';build=Path('/tmp/athena-b4-audit-san-20261016-1')
names=['package_operation_test','package_operation_scanner_test','generation_operation_test','operational_request_archive_test','operational_request_runtime_test','operational_activation_test','transaction_gc_protocol_test']
commands=[['cmake','-S',str(root),'-B',str(build),'-DCMAKE_BUILD_TYPE=Debug','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined','-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined'],['cmake','--build',str(build),'--target',*names,'-j2'],['ctest','--test-dir',str(build),'-R','^('+'|'.join(names)+')$','--output-on-failure','--output-junit',str(out/'sanitizer-junit.xml')]]
env=os.environ.copy();env.update(ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
(out/'sanitizer-commands.json').write_text(json.dumps({'commands':commands,'environment':{k:env[k] for k in ['ASAN_OPTIONS','UBSAN_OPTIONS']},'clean_build':not build.exists(),'LeakSanitizer':'NOT EXECUTED'},indent=2)+'\n')
for label,cmd in zip(['configure','build','tests'],commands):
 print('SAN',label,flush=True)
 with (out/f'sanitizer-{label}.txt').open('w') as log:rc=subprocess.run(cmd,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT).returncode
 if rc:raise SystemExit(rc)
for name in ['CMakeCache.txt','compile_commands.json']:shutil.copyfile(build/name,out/('sanitizer-'+name))
(out/'sanitizer-BUILD.sha256').write_text(''.join(hashlib.sha256((build/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in ['libathena_core.so',*names]))
print('SAN completed',flush=True)
