# Read-only retail GALE01 rev2 collector; hardware execute breakpoints only.
import gdb,json,struct,os,time
from pathlib import Path
ROOT=Path(os.environ.get('MELEE_REFERENCE_WORK','work')).resolve()
ROOT.mkdir(parents=True,exist_ok=True)
PIPES=ROOT/'reference-oracle-user/Pipes'
LOG=(ROOT/'reference-input-intake.jsonl').open('a')
OUT=(ROOT/'reference-retail-intake.jsonl').open('a')
fighters={};tick=0;enabled=False

def memory(addr,n):
 if not 0x80000000<=addr<0x81800000 or n>1024 or addr+n>0x81800000:raise RuntimeError('Reference memory range rejected')
 return bytes(gdb.selected_inferior().read_memory(addr,n))
def word(addr):return struct.unpack('>I',memory(addr,4))[0]
def random_state():
 pointer=word(0x804d5f94)
 return {'pointer':hex(pointer),'value':word(pointer),'default_value':word(0x804d5f90)}
def emit(row):OUT.write(json.dumps(row,separators=(',',':'))+'\n');OUT.flush()
def float_value(raw):return {'bits':f'{struct.unpack(">I",raw)[0]:08x}','value':struct.unpack('>f',raw)[0]}
def sample(fp):
 head=memory(fp,0x100);anim=memory(fp+0x894,16);input_data=memory(fp+0x620,0x6c);collision=memory(fp+0x794,0xfc)
 def u(o):return struct.unpack_from('>I',head,o)[0]
 def v(o):return [float_value(head[o+i:o+i+4]) for i in (0,4,8)]
 return {'kind':u(4),'spawn':u(8),'slot':head[12],'stocks':struct.unpack('>b',memory(0x80453080+head[12]*0xe90+0x8e,1))[0],'motion':u(16),'animation':u(20),'facing':float_value(head[44:48]),'position':v(0xb0),'previous':v(0xbc),'velocity':v(0x80),'knockback_velocity':v(0x8c),'ground_air':u(0xe0),'ground_velocity':float_value(head[0xec:0xf0]),'frame':float_value(anim[:4]),'frame_speed':float_value(anim[8:12]),'input_hex':input_data.hex(),'collision_hex':collision.hex(),'collision_prefix_hex':memory(fp+0x6f0,0xa4).hex(),'fighter_flags_hex':memory(fp+0x2218,0x10).hex(),'collision_generation':word(0x804d64ac),'damage':float_value(memory(fp+0x1830,4)),'callbacks':[word(fp+o) for o in (0x21a0,0x21a4,0x21a8)]}
class Created(gdb.Breakpoint):
 def __init__(self):super().__init__('*0x800693a8',gdb.BP_HARDWARE_BREAKPOINT,internal=True);self.silent=True
 def stop(self):
  try:
   gobj=int(gdb.parse_and_eval('$r3'));fp=word(gobj+0x2c);row=sample(fp);fighters[row['slot']]=fp
   emit({'event':'Fighter_Create_return','gobj':hex(gobj),'fighter':hex(fp),'state':row,'rng':random_state()})
  except Exception as e:emit({'event':'collector_error','phase':'create','error':str(e)})
  return False
class Tick(gdb.Breakpoint):
 def __init__(self):super().__init__('*0x80390eb4',gdb.BP_HARDWARE_BREAKPOINT,internal=True);self.silent=True
 def stop(self):
  global tick
  tick+=1
  if enabled:
   try:
    stage=memory(0x8049e6c8,0x8c)
    rows=[sample(fp) for slot,fp in sorted(fighters.items())]
    emit({'event':'scheduler_return','tick':tick,'game_frame':word(0x8046B6C4),'phase':'HSD_GObj_80390CFC_return','grkind':struct.unpack_from('>I',stage,0x88)[0],'stage_camera_blast_hex':stage[:0x80].hex(),'fighters':rows,'rng':random_state()})
   except Exception as e:emit({'event':'collector_error','phase':'tick','error':str(e)})
  return False
class Capture(gdb.Command):
 def __init__(self):super().__init__('ref-capture',gdb.COMMAND_USER)
 def invoke(self,args,from_tty):
  global enabled
  enabled=args.strip()!='off';print('Reference collection',enabled,'known fighter slots',list(fighters))
Created();Tick();Capture()
print('Read-only original create/scheduler collector installed')

class RefGame(gdb.Command):
 def __init__(self):super().__init__('ref-game',gdb.COMMAND_USER)
 def invoke(self,args,from_tty):
  words=args.split();frames=int(words[0]);pad=int(words[1]) if len(words)>1 else 1
  command=' '.join(words[2:]);start=word(0x8046B6C4)
  if command:
   fd=os.open(PIPES/f'pad{pad}',os.O_WRONLY|os.O_NONBLOCK)
   os.write(fd,(command+'\n').encode());os.close(fd)
  LOG.write(json.dumps({'time':time.time(),'game_frames':frames,'start_game_frame':start,'pad':pad,'command':command})+'\n');LOG.flush()
  for _ in range(frames*32+32):
   gdb.execute('continue',to_string=True)
   if (word(0x8046B6C4)-start)&0xffffffff>=frames:
    print('Advanced',frames,'retail game frames');return
  raise RuntimeError('Retail frame counter did not advance within bounded scheduler returns')
RefGame()
