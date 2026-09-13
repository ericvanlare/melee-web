"""Source-driven original Melee CSS/SSS preparation for CPU references.

``render_source_driver`` returns this readable GDB/Python program. It runs in
an owned Dolphin process, reads setup state from the original game, and drives
CSS/SSS through owned controller pipes. A missing source-verified availability
bit raises instead of writing game memory.
"""
from __future__ import annotations

from pathlib import Path

# Copied from the successful root-menu-probe-v2 route. Keep this readable so
# reviewers can audit every source read and controller transition.
DRIVER_SOURCE = r'''
import gdb, os, struct, json
from pathlib import Path

PIPES=Path(os.environ['MELEE_REPLAY_REFERENCE_WORK']).parent/'user/Pipes'
TARGET_PATH=Path(os.environ['MELEE_CHECKPOINT_TARGET_JSON']).resolve()
TARGET=json.loads(TARGET_PATH.read_text())
EXPECTED=TARGET['expected_setup']
EXPECTED_TIMER=EXPECTED['time_limit_seconds']//60
EXPECTED_STOCKS=EXPECTED['players'][0]['stocks']
EXPECTED_PAUSE=0 if EXPECTED['disable_pausing'] else 1
EXPECTED_STAGE=EXPECTED['stage']
EXPECTED_PLAYERS=EXPECTED['players']
EXPECTED_TEAMS=[int(player.get('team',0)) for player in EXPECTED_PLAYERS]
def mem(a,n):
    if not 0x80000000<=a<=0x81800000-n:raise RuntimeError('Invalid observed pointer')
    return bytes(gdb.selected_inferior().read_memory(a,n))
def u32(a):return struct.unpack('>I',mem(a,4))[0]
def scene_kind():
    ptr=u32(0x804D6720)
    if not 0x80000000<=ptr<=0x81800000-1:return -1
    return mem(ptr,1)[0]
BUTTON_BITS={'D_LEFT':1,'D_RIGHT':2,'D_DOWN':4,'D_UP':8,'Z':16,'R':32,'L':64,'A':256,'B':512,'X':1024,'Y':2048,'START':4096}
BUTTON_NAMES=('D_LEFT','D_RIGHT','D_DOWN','D_UP','Z','R','L','A','B','X','Y','START')
pad_state=[{'buttons':0,'x':.5,'y':.5,'cx':.5,'cy':.5,'left':0.,'right':0.} for _ in range(4)]
def _write_state(port):
    s=pad_state[port]
    lines=[('PRESS ' if s['buttons'] & BUTTON_BITS[name] else 'RELEASE ')+name for name in BUTTON_NAMES]
    lines += [f"SET MAIN {s['x']:.17g} {s['y']:.17g}",f"SET C {s['cx']:.17g} {s['cy']:.17g}",f"SET L {s['left']:.17g}",f"SET R {s['right']:.17g}"]
    fd=os.open(PIPES/f'pad{port+1}',os.O_WRONLY|os.O_NONBLOCK)
    try:
        payload=('\n'.join(lines)+'\n').encode()
        if os.write(fd,payload)!=len(payload): raise RuntimeError('Incomplete setup controller pipe write')
    finally:os.close(fd)
def command(port,text):
    words=text.split()
    if not words: raise RuntimeError('Empty setup controller command')
    if words[0] in ('PRESS','RELEASE') and len(words)==2:
        if words[1] not in BUTTON_BITS: raise RuntimeError('Unknown setup button '+words[1])
        if words[0]=='PRESS': pad_state[port]['buttons'] |= BUTTON_BITS[words[1]]
        else: pad_state[port]['buttons'] &= ~BUTTON_BITS[words[1]]
    elif words[:2]==['SET','MAIN'] and len(words)==4:
        pad_state[port]['x'],pad_state[port]['y']=float(words[2]),float(words[3])
    elif words[:2]==['SET','C'] and len(words)==4:
        pad_state[port]['cx'],pad_state[port]['cy']=float(words[2]),float(words[3])
    elif words[:2] in (['SET','L'],['SET','R']) and len(words)==3:
        pad_state[port]['left' if words[1]=='L' else 'right']=float(words[2])
    else: raise RuntimeError('Unknown setup controller command '+text)
    _write_state(port)
def step(count):
    previous=u32(0x80479D58);completed=0
    for attempt in range(count*8+64):
        # Pipe commands are persistent controller state.  Publish once when
        # a command changes it; rewriting the full button/axis packet on every
        # stopped scheduler tick can queue stale PRESS/RELEASE lines behind
        # the emulated PAD reader and change the source menu history.
        gdb.execute('continue',to_string=True)
        current=u32(0x80479D58)
        if current==previous:continue
        if current!=(previous+1)&0xffffffff and current!=0:
            raise RuntimeError(f'Unexpected scene counter {previous}->{current}')
        completed+=1;previous=current
        if completed==count:return
    raise RuntimeError('Setup did not progress')

def css(port,kind):
    icons=[mem(0x803F0B24+i*28,28) for i in range(25)]
    targets=[i for i,r in enumerate(icons) if r[1]==kind and r[2]>=1]
    if len(targets)!=1:raise RuntimeError(f'Character {kind} not available')
    target=targets[0];bounds=struct.unpack_from('>ffff',icons[target],12)
    cursor=mem(u32(0x804A0BC0+port*4),20);model=mem(u32(0x804A0BD0+port*4),24)
    selected=mem(0x803F0DFC+port*36+14,1)[0]
    return {'port':cursor[4],'cursor_state':cursor[5],'cursor_target':cursor[6],
            'model_owner':model[5],
            'held':cursor[6] if cursor[5]==1 and cursor[6]<4 else -1,
            'selected':icons[selected][1] if selected<25 else -1,
            'cursor':struct.unpack_from('>ff',cursor,12),'model':struct.unpack_from('>ff',model,8),
            'bounds':bounds,'target':target}
def select(port,kind):
    for attempt in range(400):
        s=css(port,kind);x,y=s['cursor'];mx,my=s['model'];l,r,t,b=s['bounds']
        if s['held']<0 and s['selected']==kind and l<mx<r and b<my<t:
            command(port,'SET MAIN .5 .5');step(4);print('Selected',s);return
        if s['held']>=0 and s['held']!=port:raise RuntimeError('Holding wrong player token')
        tx,ty=((l+r)/2-2.7,(t+b)/2+2) if s['held']==port else (mx-3.8,my+2.6)
        # The original cursor joins an unselected door only above y=0.2.
        # Its vacant token rests at y=-3; the ordinary pickup offset would
        # instead click the player-type toggle below the character grid.
        if s['held']<0 and s['selected']<0:
            ty=max(1.0,ty)
        dx=tx-x;dy=ty-y
        if abs(dx)<.7 and abs(dy)<.7:
            command(port,'SET MAIN .5 .5');step(2)
            command(port,'PRESS A');step(2);command(port,'RELEASE A');step(4)
        else:
            # Direction only; original CSS owns cursor movement and selection.
            command(port,f'SET MAIN {1 if dx>.7 else 0 if dx<-.7 else .5} {1 if dy>.7 else 0 if dy<-.7 else .5}')
            step(1)
        if attempt%40==0:print('Selecting',s,flush=True)
    raise RuntimeError('CSS selection budget exhausted')

def menu_state():
    raw=mem(0x804A04F0,0x18)
    return {'cur':raw[0],'prev':raw[1],'hovered':struct.unpack_from('>H',raw,2)[0],
            'confirmed':raw[4],'entering':raw[0x11]}

def gobj_processes():
    out=[]; g=u32(0x804D782C); seen=set()
    while g and g not in seen and len(out)<80:
        seen.add(g); funcs=[]; proc=u32(g+24); pseen=set()
        while proc and proc not in pseen and len(funcs)<20:
            pseen.add(proc); funcs.append(hex(u32(proc+20))); proc=u32(proc)
        out.append((hex(g),funcs)); g=u32(g+8)
    return out

def rules_state():
    base=u32(0x804D3EE0)
    raw=mem(base+0x1850,0x18)
    # gmm_x1868 is the save-data subobject; preference fields are relative
    # to it (DOL gmMainLib_8015CC58 returns base+0x1CB0).
    save=base+0x1868
    return {'mode':raw[2],'time_limit':raw[3],'stock_count':raw[4],
            'stock_time_limit':raw[8],'friendly_fire':raw[9],'pause':raw[10],
            'score_display':raw[11],'item_frequency':mem(save+0x448,1)[0],
            'item_mask':struct.unpack('>Q',mem(save+0x450,8))[0],
            'rumble':list(mem(save+0x458,4))}

def return_to_css():
    # The owned donor state is paused at the original Final Destination SSS
    # boundary.  Use the retail SSS back route to reopen CSS before changing
    # the P2 type/level; this keeps the transition source-owned.
    for port in range(len(EXPECTED_PLAYERS)): command(port,'SET MAIN .5 .5')
    step(12)
    command(0,'PRESS B');step(8);command(0,'RELEASE B')
    for _ in range(600):
        if scene_kind()==8:
            step(60)
            return
        if scene_kind()==42:
            # The donor user has an ordinary Melee GCI.  Returning through
            # the retail scene can surface its normal save prompt; accept the
            # prompt with P1 and keep waiting for CSS to rebuild.
            command(0,'PRESS A');step(4);command(0,'RELEASE A');step(30)
            continue
        step(1)
    raise RuntimeError(f'SSS did not return to CSS: scene={scene_kind()} mode={mem(0x80479D30,6).hex()}')

def pulse(port,name,settle=8):
    # The pipe is polled asynchronously by the emulated PAD device. Keep a
    # menu direction present for several source ticks so the retail triggered
    # and repeat histories consume it deterministically; release through the
    # menu cooldown before the next action.
    command(port,'PRESS '+name);step(4 if name.startswith('D_') else 2)
    command(port,'RELEASE '+name);step(settle)

def wait_menu(cur,limit=240):
    for _ in range(limit):
        state=menu_state()
        if scene_kind()==1 and state['cur']==cur and struct.unpack('>H',mem(0x804D6BC8,2))[0]==0:return state
        step(1)
    raise RuntimeError(f'menu transition did not reach ready {cur}: {menu_state()}')

def move_menu_selection(target,limit=24):
    for _ in range(limit):
        state=menu_state()
        if state['hovered']==target:return state
        count={0:5,2:5,4:5,13:7,15:6}[state['cur']]
        # These menus wrap; choose the shortest ordinary D-pad path.
        current=state['hovered']
        down=(target-current)%count
        up=(current-target)%count
        button='D_DOWN' if down<=up else 'D_UP'
        pulse(0,button)
    raise RuntimeError(f'menu selection did not reach {target}: {menu_state()}')

def set_rules_via_original_menu():
    # SSS -> CSS -> main menu via the retail LR+START back chord.
    return_to_css()
    print('CSS READY FOR RULES',scene_kind(),flush=True)
    command(0,'PRESS L');command(0,'PRESS R');step(4);command(0,'PRESS START');step(12)
    command(0,'RELEASE L');command(0,'RELEASE R');command(0,'RELEASE START')
    # The LR+START handler requests GM_MENU asynchronously.  The old menu
    # cursor remains visible in memory until that mode is entered, so wait on
    # the pinned game-mode byte rather than treating a stale cursor as ready.
    for _ in range(600):
        if mem(0x80479D30,1)==b'\x01': break
        step(1)
    else: raise RuntimeError(f'GM_MENU transition did not complete: scene={scene_kind()} mode={mem(0x80479D30,6).hex()} padcopy={mem(0x804C20BC,68).hex()} menu={menu_state()}')
    # The first GM_MENU entry can display the ordinary memory-card overwrite
    # prompt. Its default is No; accept each visible card prompt with P1 A,
    # then allow the save-card confirmation to settle before waiting for the
    # real menu flow. Scene kind 42 is GS_MEMCARD in the pinned source.
    for _ in range(4):
        if scene_kind()!=42: break
        command(0,'PRESS A');step(4);command(0,'RELEASE A');step(30)
    # GM_MENU mode becomes current before its onEnter consumes
    # force_main_menu and rebuilds the menu object. Wait for both source
    # markers instead of reading the stale SSS/Rules flow while it loads.
    base_dbg=u32(0x804D3EE0)
    for _ in range(1800):
        if scene_kind()==42:
            pulse(0,'A',settle=30)
            continue
        state=menu_state()
        if scene_kind()==1 and mem(0x80479D30,1)==b'\x01' and mem(base_dbg+0x1850,1)==b'\x00' and state['cur']==0:
            break
        step(1)
    else:
        raise RuntimeError(f'GM_MENU onEnter did not complete: mode={mem(0x80479D30,6).hex()} menu={menu_state()} rules={mem(base_dbg+0x1850,0x18).hex()} gobs={gobj_processes()}')
    wait_menu(0)
    print('Main menu ready',menu_state(),flush=True)
    # Original Options > Rumble: each port's A toggles its own setting.
    move_menu_selection(3);pulse(0,'A');wait_menu(4)
    move_menu_selection(0);pulse(0,'A');wait_menu(19);step(90)
    for port,wanted in enumerate([int(p['rumble_enabled']) for p in EXPECTED_PLAYERS]):
        for attempt in range(6):
            if rules_state()['rumble'][port]==wanted:break
            pulse(port,'A',settle=30)
        else:raise RuntimeError('Rumble setting did not change through original input')
    print('Rumble verified',rules_state()['rumble'],flush=True)
    pulse(0,'B');wait_menu(4);pulse(0,'B');wait_menu(0)
    move_menu_selection(1);pulse(0,'A');wait_menu(2)
    move_menu_selection(3);pulse(0,'A');wait_menu(13)
    print('Rules menu ready',menu_state(),flush=True)
    # Main Rules menu row 1 is the source stock count; change it before the extra rules submenu.
    move_menu_selection(1)
    for _ in range(100):
        value=menu_state()['confirmed']
        if value==EXPECTED_STOCKS:break
        pulse(0,'D_RIGHT' if value<EXPECTED_STOCKS else 'D_LEFT')
    else:raise RuntimeError('Stock count did not reach the requested original menu value')
    move_menu_selection(6);pulse(0,'A');wait_menu(15)
    for row,wanted in ((0,EXPECTED_TIMER),(2,EXPECTED_PAUSE)):
        move_menu_selection(row)
        for attempt in range(100):
            state=menu_state()
            if state['hovered']!=row:raise RuntimeError('Rule adjustment changed the selected row')
            value=state['confirmed']
            if value==wanted:break
            pulse(0,'D_RIGHT' if value<wanted else 'D_LEFT')
        else:raise RuntimeError('Rule value did not reach requested setting')
        print('Rule edited',row,menu_state(),flush=True)
    # B saves the authored values before validating the persistent rules.
    pulse(0,'B');wait_menu(13)
    observed=rules_state()
    if observed['mode']!=1 or observed['stock_count']!=EXPECTED_STOCKS or observed['stock_time_limit']!=EXPECTED_TIMER or observed['pause']!=EXPECTED_PAUSE:
        raise RuntimeError(f'final timer/stock/pause rules differ: {observed}')
    if observed['item_frequency']!=0xFF or observed['item_mask']!=0xFFFFFFFFFFFFFFFF:
        raise RuntimeError(f'base items are not ordinary items-off: {observed}')
    if observed['rumble'][:len(EXPECTED_PLAYERS)] != [int(p['rumble_enabled']) for p in EXPECTED_PLAYERS]:
        raise RuntimeError(f'rumble does not match donor: {observed}')
    print('Rules verified before CSS',observed,flush=True)
    pulse(0,'START',settle=12)
    for attempt in range(600):
        if scene_kind()==8:break
        if scene_kind()==42:
            pulse(0,'A',settle=30)
        else:step(1)
    else:raise RuntimeError(f'CSS did not enter: mode={mem(0x80479D30,6).hex()} scene={scene_kind()}')
    step(90)
    return rules_state()

def css_player(port):
    pointer=u32(0x804D6CB0)
    if not (0x80000000<=pointer<=0x81800000-0xA0):
        raise RuntimeError(f'invalid CSSData pointer {pointer:#x}')
    # CSSData's VsModeData starts at +0x10; StartMeleeRules is 0x60 bytes.
    raw=mem(pointer+0x70+port*0x24,0x24)
    return {'port':port+1,'character_kind':struct.unpack_from('>b',raw,0)[0],
            'slot_type':raw[1],'stocks':struct.unpack_from('>b',raw,2)[0],
            'team':raw[9],'costume':raw[3],'rumble_byte':raw[0x0c],
            'cpu_kind':raw[0x0e],'cpu_level':raw[0x0f]}

def css_door(door):
    # CSSDoor has four 32-bit bounds after the byte fields; the retail stride
    # is 0x24 bytes (the two toggle bounds are at +0x14/+0x18).
    raw=mem(0x803F0DFC+door*0x24,0x24)
    return {'p_kind':raw[0x0b],'p_kind_prev':raw[0x0c],
            'toggle_left':struct.unpack_from('>f',raw,0x14)[0],
            'toggle_right':struct.unpack_from('>f',raw,0x18)[0],
            'cpu_slider_index':raw[7]}

def jobj_at(root,target):
    """Read the original HSD pre-order JObj walk used by lb_80011E24."""
    def ptr(address):
        value=u32(address)
        if value and not 0x80000000<=value<=0x81800000-0x88:
            raise RuntimeError('Invalid observed JObj pointer')
        return value
    jobj=root;current=0;seen=set()
    while jobj and current<target:
        if jobj in seen: raise RuntimeError('Invalid JObj cycle')
        seen.add(jobj)
        flags=u32(jobj+0x14)
        child=ptr(jobj+0x10);next_node=ptr(jobj+8)
        if not (flags & 0x1000) and child:
            jobj=child
        elif next_node:
            jobj=next_node
        else:
            saved=jobj
            while True:
                parent=ptr(saved+0x0c)
                if not parent:
                    jobj=0;break
                sibling=ptr(parent+8)
                if sibling:
                    jobj=sibling;break
                saved=parent
        current+=1
    return jobj if current==target else 0

def jobj_world_xy(jobj):
    if not jobj: raise RuntimeError('Original CPU slider JObj is absent')
    parent=u32(jobj+0x0c)
    if parent:
        return (struct.unpack('>f',mem(jobj+0x50,4))[0],
                struct.unpack('>f',mem(jobj+0x60,4))[0])
    return (struct.unpack('>f',mem(jobj+0x38,4))[0],
            struct.unpack('>f',mem(jobj+0x3c,4))[0])

def cpu_slider_xy(door):
    root=u32(0x804D6CC0)
    index=css_door(door)['cpu_slider_index']
    return jobj_world_xy(jobj_at(root,index))

def cursor_state(port):
    raw=mem(u32(0x804A0BC0+port*4),20)
    return {'x':struct.unpack_from('>f',raw,12)[0],
            'y':struct.unpack_from('>f',raw,16)[0],
            'mode':raw[5],'target':raw[6]}

def move_cursor(port,tx,ty,tolerance=.7,limit=180):
    for _ in range(limit):
        s=cursor_state(port);dx=tx-s['x'];dy=ty-s['y']
        if abs(dx)<tolerance and abs(dy)<tolerance:
            command(port,'SET MAIN .5 .5');step(1);return cursor_state(port)
        command(port,f'SET MAIN {1 if dx>tolerance else 0 if dx<-tolerance else .5} {1 if dy>tolerance else 0 if dy<-tolerance else .5}')
        step(1)
    raise RuntimeError(f'CSS cursor did not reach target ({tx},{ty}): {cursor_state(port)}')

def set_cpu_mode(door):
    # The original CSS toggle is the small HMN/CPU box centred at y=-2.2.
    bounds=css_door(door)
    tx=(bounds['toggle_left']+bounds['toggle_right'])/2
    # Source movement advances in coarse one-tick increments; accept the
    # nearest stable point when it lies inside the retail y hitbox.
    move_cursor(door,tx,-2.2,tolerance=.7)
    command(door,'PRESS A');step(2);command(door,'RELEASE A');step(30)
    if css_door(door)['p_kind']!=1 or css_player(door)['slot_type']!=1:
        raise RuntimeError(f'Original CSS CPU toggle did not select CPU: door={css_door(door)} player={css_player(door)}')

def set_cpu_level(door,wanted):
    if not 1<=wanted<=9: raise RuntimeError('CPU level must be in original range 1..9')
    current=css_player(door)['cpu_level']
    if current==0: current=1
    if current!=wanted:
        world_x,world_y=cpu_slider_xy(door)
        # The slider knob is shifted by 1.25 source units per level. Grab the
        # currently shown knob, then use the original held-slider update path.
        move_cursor(door,-2.9+world_x,1.7+world_y,tolerance=.9)
        command(door,'PRESS A');step(2);command(door,'RELEASE A');step(1)
        for _ in range(8):
            state=cursor_state(door)
            if state['mode']==1 and state['target']==door+4: break
            step(1)
        else: raise RuntimeError(f'Original CPU slider was not grabbed: {cursor_state(door)}')
        for _ in range(12):
            current=css_player(door)['cpu_level']
            if current==wanted: break
            if current<wanted: command(door,'SET MAIN 1 .5')
            else: command(door,'SET MAIN 0 .5')
            step(1)
        if css_player(door)['cpu_level']!=wanted:
            raise RuntimeError(f'Original CPU slider did not reach {wanted}: {css_player(door)}')
        # The source updates a grabbed slider from cursor position before
        # dropping it. Release the stick first so A does not move one more
        # difficulty step on the drop tick (the endpoints mask this mistake).
        command(door,'SET MAIN .5 .5');step(1)
        command(door,'PRESS A');step(2);command(door,'RELEASE A');step(20)
    if css_player(door)['slot_type']!=1 or css_player(door)['cpu_level']!=wanted:
        raise RuntimeError(f'CPU slider setup mismatch: {css_player(door)}')

def set_costume(port,expected):
    for _ in range(8):
        current=css_player(port)['costume']
        if current==expected:return
        command(port,'PRESS X');step(1);command(port,'RELEASE X');step(5)
    raise RuntimeError(f'costume did not reach {expected}: {css_player(port)}')

def sss(kind):
    target=None
    for i in range(30):
        tile=mem(0x803F06D0+i*28,28)
        if tile[11]==kind and tile[8]>=2:target=struct.unpack_from('>I',tile)[0]
    if target is None:
        print('Stage rows',[(i,mem(0x803F06D0+i*28+8,4).hex()) for i in range(30)],'CSS flags',mem(0x804D6CF2,6).hex(),'unlock words',mem(u32(0x804D3EE0)+0x1868,4).hex(),flush=True)
        raise RuntimeError('Requested stage is locked or SSS has not entered')
    cursor=None;g=u32(u32(0x804D782C)+20);seen=set()
    while g:
        if g in seen or len(seen)>100:raise RuntimeError('Invalid menu object chain')
        seen.add(g);proc=u32(g+24);pseen=set()
        while proc:
            if proc in pseen or len(pseen)>100:raise RuntimeError('Invalid menu process chain')
            pseen.add(proc)
            if u32(proc+20)==0x8025A310:
                if cursor is not None:raise RuntimeError('Ambiguous SSS cursor')
                cursor=u32(g+40)
            proc=u32(proc)
        g=u32(g+8)
    if cursor is None:raise RuntimeError('SSS cursor absent')
    def xy(j):return [struct.unpack('>f',mem(j+offset,4))[0] for offset in (0x50,0x60)]
    return {'cursor':xy(cursor),'target':xy(target),'selected':mem(0x804D6CAE,1)[0]}
def selected_stage_kind():
    selected=mem(0x804D6CAE,1)[0]
    if selected>=30:return -1
    return mem(0x803F06D0+selected*28+11,1)[0]

def select_stage(kind):
    # Stage cursor coordinates are zero during the source SSS intro; wait for
    # the pinned intro boundary before comparing coordinates. The selected
    # tile kind is the source-owned readiness check, with a neutral recheck.
    if scene_kind()!=9: raise RuntimeError(f'SSS stage steering entered in scene {scene_kind()}')
    for _ in range(45): step(1)
    for attempt in range(300):
        try:
            s=sss(kind)
        except RuntimeError as error:
            # SSS archives/processes are created asynchronously after VS Start;
            # keep advancing ordinary source ticks until the real cursor exists.
            if str(error) not in ('SSS cursor absent', 'Requested stage is locked or SSS has not entered'):
                raise
            step(1)
            continue
        x,y=s['cursor'];tx,ty=s['target'];dx=tx-x;dy=ty-y
        if abs(dx)<1.3 and abs(dy)<1.3:
            if selected_stage_kind()!=kind:
                command(0,'SET MAIN .5 .5');step(1)
                continue
            command(0,'SET MAIN .5 .5');step(6)
            if selected_stage_kind()!=kind:
                raise RuntimeError(f'SSS selected tile changed during neutral readiness: expected {kind}, got {selected_stage_kind()}')
            print('SSS ready',sss(kind),flush=True);return
        command(0,f'SET MAIN {1 if dx>1.3 else 0 if dx<-1.3 else .5} {1 if dy>1.3 else 0 if dy<-1.3 else .5}');step(1)
        if attempt%30==0:print('Steering SSS',s,flush=True)
    raise RuntimeError('SSS steering budget exhausted')

# Preparation only, before a new checkpoint. The owned baseline must
# already contain every requested CSS/SSS availability bit. This driver
# performs no source-memory, register, RNG, fighter, rules, CPU, or
# active-match writes; it rejects a baseline that needs an unlock.
assert mem(0x8015ED90,4)==bytes.fromhex('38631868')
assert mem(0x8015EDA8,4)==bytes.fromhex('3863186a')
CHAR_UNLOCKS={9:(2,9,2,'Marth bit2'),20:(5,19,2,'Falco bit5')}
STAGE_UNLOCKS={28:(8,28,3,'Dream Land bit8'),31:(6,36,2,'Battlefield bit6'),32:(7,37,3,'Final Destination bit7')}
base=u32(0x804D3EE0);address=base+0x1868
before=mem(address-8,20);chars,stages=struct.unpack('>HH',mem(address,4))
char_bits=stage_bits=0;changes=[]
for player in EXPECTED_PLAYERS:
    kind=player['character_kind']
    if kind in CHAR_UNLOCKS:
        row,source_kind,width,label=CHAR_UNLOCKS[kind]
        assert mem(0x803B78C8+row*6,width)==bytes([row,source_kind])
        char_bits |= 1<<row; changes.append(label)
if EXPECTED_STAGE in STAGE_UNLOCKS:
    row,ground_kind,width,label=STAGE_UNLOCKS[EXPECTED_STAGE]
    assert mem(0x803B790C+row*3,width)==bytes([row,ground_kind]+([19 if EXPECTED_STAGE==28 else 18] if width==3 else []))
    stage_bits |= 1<<row; changes.append(label)
new_words=struct.pack('>HH',chars|char_bits,stages|stage_bits)
if new_words != mem(address,4):
    raise RuntimeError('Owned baseline lacks a requested source CSS/SSS unlock; refusing source-memory mutation')
after=mem(address-8,20)
assert before[:8]==after[:8] and before[12:]==after[12:]
(Path(os.environ['MELEE_REPLAY_REFERENCE_WORK'])/'checkpoint-setup.json').write_text(json.dumps({
 'purpose':'checkpoint preparation only; not reference capture', 'address':hex(address),
 'before':before.hex(),'after':after.hex(),'changes':sorted(set(changes)),
 'source_verified_unlocks':False,'no_code_rng_fighter_rules_or_active_match_state_changes':True},indent=2)+'\n')

# The declared donor snapshot is at an original SSS boundary. Re-enter the
# original menus to establish this recipe; source code owns all game state.
set_rules_via_original_menu()
rules_before=rules_state()
expected_css=[(p['character_kind'],p['costume'],p['player_type']) for p in EXPECTED_PLAYERS]
print('CSS initial',[(p['character_kind'],p['costume']) for p in EXPECTED_PLAYERS],flush=True)
for port,player in enumerate(EXPECTED_PLAYERS):
    select(port,player['character_kind'])
    set_costume(port,player['costume'])
players=[css_player(i) for i in range(len(EXPECTED_PLAYERS))]
print('CSS completed',players,flush=True)
if [(p['character_kind'],p['costume'],p['slot_type']) for p in players] != [(k,c,0) for k,c,_ in expected_css]:
    raise RuntimeError(f'CSS setup mismatch: expected {expected_css}, got {players}')
for door,player in enumerate(EXPECTED_PLAYERS[1:],1):
    set_cpu_mode(door)
    set_cpu_level(door,player['cpu_level'])
players=[css_player(i) for i in range(len(EXPECTED_PLAYERS))]
print('CPU configured through original CSS',players,flush=True)
if [(p['character_kind'],p['costume'],p['slot_type']) for p in players] != expected_css:
    raise RuntimeError(f'CSS CPU setup mismatch: expected {expected_css}, got {players}')
if [p['team'] for p in players] != EXPECTED_TEAMS:
    raise RuntimeError(f'CSS team setup mismatch: expected {EXPECTED_TEAMS}, got {players}')
for _ in range(120):
    if mem(0x804D6CF2,1)==b'\0' and mem(0x804D6CF7,1)!=b'\0':break
    step(1)
else:raise RuntimeError('CSS did not become ready for Start')
print('CSS Start enabled',mem(0x804D6CF2,6).hex(),flush=True)
# Retail-ready CSS consumes exactly one START tick for the SSS handoff.
command(0,'PRESS START');step(1);command(0,'RELEASE START');step(1)
for attempt in range(120):
    if scene_kind()==9: break
    if scene_kind()==42:
        # Starting the edited CSS can also surface the normal source card
        # prompt before SSS is rebuilt.  Acknowledge it through P1 input.
        command(0,'PRESS A');step(4);command(0,'RELEASE A');step(30)
        continue
    step(1)
else: raise RuntimeError('SSS did not enter after CSS Start')
select_stage(EXPECTED_STAGE)
ready={'stage':sss(EXPECTED_STAGE),'rules':rules_state(),'players':players,'rules_before_css':rules_before,
       'source_stage_kind':EXPECTED_STAGE,'expected_setup':EXPECTED}
if ready['rules']['stock_time_limit']!=EXPECTED_TIMER or ready['rules']['pause']!=EXPECTED_PAUSE:
    raise RuntimeError(f'final setup rules mismatch: {ready}')
(Path(os.environ['MELEE_REPLAY_REFERENCE_WORK'])/'checkpoint-ready.json').write_text(json.dumps(ready,sort_keys=True)+'\n')
print('CHECKPOINT READY',json.dumps(ready,sort_keys=True),flush=True)
'''

def render_source_driver() -> str:
    """Return the self-contained source-menu preparation driver."""
    return DRIVER_SOURCE


def render_driver() -> str:
    """Compatibility alias used by preparation scripts."""
    return render_source_driver()


def write_driver(path: str | Path) -> Path:
    """Write one owned copy of the source driver and return its path."""
    destination = Path(path)
    destination.write_text(DRIVER_SOURCE, encoding="utf-8")
    return destination


def validate_driver() -> None:
    """Reject accidental game-memory writes or diagnostic-only proof code."""
    if "write_" + "memory(" in DRIVER_SOURCE:
        raise ValueError("source menu driver must not write game memory")
    if "print('" + "CHORD'" in DRIVER_SOURCE:
        raise ValueError("source menu driver must not retain chord diagnostics")
    compile(DRIVER_SOURCE, "retail_cpu_menu_prepare.gdb.py", "exec")


validate_driver()
