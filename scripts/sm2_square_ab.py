#!/usr/bin/env python3
"""SM2 square A/B against pinned pre-optimization source.
--board overwrites F103 Flash; restore the network application afterwards.
"""
import argparse
from pathlib import Path
import subprocess,os,re,json,time,sys
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc',required=True)
parser.add_argument('--board',action='store_true')
parser.add_argument('--output',type=Path,default=Path('bin/sm2-sqr/pair'))
args=parser.parse_args()
root=Path(__file__).resolve().parents[1];os.chdir(root)
out=args.output.resolve();out.mkdir(exist_ok=True,parents=True)
board=args.board
cc=Path(args.cc).resolve()
baseline=out/'baseline.c'
baseline.write_bytes(subprocess.check_output(['git','show','01faaa8:crypto/src/sm2_arith.c']))
env=os.environ.copy();env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
def run(c):
 r=subprocess.run([str(x) for x in c],capture_output=True,env=env)
 if r.returncode:
  print(r.stdout.decode(errors='replace'));print(r.stderr.decode(errors='replace'));r.check_returncode()
 return r
flags=['-std=c99','-O2','-Wall','-Wextra','-ffunction-sections','-fdata-sections','-fstack-usage','-Icrypto/include','-Icrypto/src','-DSMC_SM2_ENC=0']
if board:flags+=['-mcpu=cortex-m3','-mthumb','-ffreestanding','-DSQR_BOARD']
objects=[];units=['crypto/src/sm2_arith.c','crypto/src/sm2.c']
# Discover every public symbol of the paired compilation units.
names=[]
for n,src in enumerate(units):
 o=out/('probe%d.o'%n);run([cc]+flags+['-c',src,'-o',o]);nm=cc.with_name(cc.name.replace('gcc','nm'))
 for line in run([nm,'--defined-only',o]).stdout.decode(errors='replace').splitlines():
  m=re.search(r'\b[TDRB] (\w+)$',line)
  if m:names.append(m[1])
rename=['-D'+n+'=before_'+n for n in names]
for variant in ('old','new'):
 for n,src in enumerate(units):
  o=out/(variant+str(n)+'.o');extra=rename+['-DSMC_SM2_DEDICATED_SQR=0'] if variant=='old' else []
  run([cc]+flags+extra+['-c',(baseline if variant=='old' and n==0 else src),'-o',o]);objects.append(o)
for n,src in enumerate(['crypto/src/sm3.c','crypto/src/sm4.c','crypto/src/hmac_sm3.c','crypto/src/smc_rng.c','apps/bench/bench_sm2_square.c']+(['embedded/cortex-m3/startup_stm32f103c8.c'] if board else [])):
 o=out/('common%d.o'%n);run([cc]+flags+['-c',(baseline if variant=='old' and n==0 else src),'-o',o]);objects.append(o)
exe=out/('ab.elf' if board else 'ab.exe');link=[cc]+objects
if board:link+=['-mcpu=cortex-m3','-mthumb','-specs=nano.specs','-nostartfiles','-Wl,--gc-sections','-Tembedded/cortex-m3/stm32f103c8.ld']
run(link+['-o',exe])
if not board:
 log=run([exe]).stdout.decode()
 import ctypes,random
 dll=out/('oracle.dll' if os.name=='nt' else 'oracle.so')
 run([cc]+flags+['-shared','crypto/src/sm2_arith.c','-o',dll])
 library=ctypes.CDLL(str(dll));Words=ctypes.c_uint32*8
 library.sm2_fp_sqr.argtypes=[ctypes.POINTER(Words),ctypes.POINTER(Words)]
 prime=int('FFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000FFFFFFFFFFFFFFFF',16)
 random_source=random.Random(0x534d32)
 values=[0,1,2,prime-1,prime,prime+1,(1<<256)-1]+[(1<<i)-1 for i in range(1,257)]+[random_source.getrandbits(256) for _ in range(10000)]
 for value in values:
  a=Words(*[(value>>(32*j))&0xffffffff for j in range(8)]);b=Words()
  library.sm2_fp_sqr(ctypes.byref(b),ctypes.byref(a))
  actual=sum(int(b[j])<<(32*j) for j in range(8))
  if actual!=value*value%prime:raise RuntimeError('Python bigint oracle mismatch')
 log+='Python bigint oracle: %d checks, 0 failures\n'%len(values)
 (out/'correctness.log').write_text(log,encoding='utf-8');print(log);sys.exit()
sys.path.insert(0,str(root/'scripts'))
from board_bench import find_symbol
from pyocd.core.helpers import ConnectHelper
from pyocd.flash.file_programmer import FileProgrammer
addr=find_symbol(exe,'square_results')
with ConnectHelper.session_with_chosen_probe(target_override='stm32f103rc',frequency=100000,options={'connect_mode':'under-reset','chip_erase':'chip'}) as s:
 t=s.board.target;FileProgrammer(s).program(str(exe));t.reset();t.resume()
 for i in range(300):
  time.sleep(.5);t.halt();v=t.read_memory_block32(addr,18);t.resume()
  if v[0]==0x53515231 and v[1]==2:break
 else:raise RuntimeError('timeout')
import hashlib
result=dict(baseline_ref='01faaa8',compiler=run([cc,'--version']).stdout.decode(errors='replace').splitlines()[0],
 flags=flags,operations=['fp_square','fp_inverse','public_key','sign','verify','ecdh','fp_mul_self'],
 method='same image; five alternating-order samples, minimum; square and multiply 64 repetitions',
 failures=v[2],checks=v[3],before=v[4:11],after=v[11:18],
 firmware_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
 source_sha256=hashlib.sha256((root/'crypto/src/sm2_arith.c').read_bytes()).hexdigest())
(out/'board.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(result)
if result['failures'] or any(x==0 for x in result['before']+result['after']):raise RuntimeError('Invalid benchmark result')
size=cc.with_name(cc.name.replace('gcc','size'))
log=run([size,out/'old0.o',out/'new0.o',exe]).stdout.decode(errors='replace')
(out/'size.log').write_text(log.replace(str(root),'PROJECT'),encoding='utf-8')
