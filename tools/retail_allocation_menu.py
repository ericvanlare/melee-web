"""Cold-boot source-menu preparation for the retail allocation-history run.

The existing :mod:`retail_cpu_menu_prepare` driver starts at an original SSS
boundary.  This adapter keeps that driver byte-for-byte as the preparation
body and inserts a bounded route from a fresh DOL boot through the first
original CSS to that SSS precondition.  The route is intentionally
controller-only: it observes the pinned scene object and scheduler boundary,
while all game state remains owned by the retail source.

The launch/collector layer is responsible for starting an unmodified DOL and
arming the scheduler breakpoint.  This module only renders the GDB/Python
driver and its focused validation helpers; it does not launch Dolphin.
"""

from __future__ import annotations

from pathlib import Path

try:
    # Tests/importers run from the repository root, where ``tools`` is a
    # namespace package.
    from tools.retail_cpu_menu_prepare import (  # noqa: F401
        DRIVER_SOURCE as _BASE_DRIVER_SOURCE,
    )
except ModuleNotFoundError:
    # GDB/collector builders also import sibling tools with their ``tools/``
    # directory directly on sys.path.
    from retail_cpu_menu_prepare import (  # type: ignore[no-redef]
        DRIVER_SOURCE as _BASE_DRIVER_SOURCE,
    )


# These values are the pinned GALE01r2 ``GameSceneKind`` values in
# ``.deps/melee/src/melee/gm/forward.h``.  Boot's actual first state is
# ``GS_MEMCARD`` in ``gmboot.c``; title START handling is in
# ``gmtitlemode.c``; the main/VS menu handoff is in ``mnmain.c``.
SCENE_TITLE = 0
SCENE_MENU = 1
SCENE_CSS = 8
SCENE_SSS = 9
SCENE_MEMCARD = 0x2A
SCENE_OPENING_MOVIE = 0x1C
SCHEDULER_RETURN = 0x80390EB4
COMMAND_LOG_NAME = "cold-boot-input-commands.jsonl"
MENU_ROUTE_TRACE_NAME = "cold-boot-menu-route.jsonl"
VS_RULES_ITEMS_TRACE_NAME = "cold-boot-vs-rules-items-route.jsonl"


_COLD_BOOT_HELPERS = rf'''
# Cold-boot route, pinned to GameSceneKind in the GALE01r2 source:
# GS_TITLE=0, GS_MENU=1, GS_CSS=8, GS_SSS=9, GS_MOVIE_OPENING=0x1c,
# GS_MEMCARD=0x2a.  gmboot.c starts in GS_MEMCARD; gmtitlemode.c accepts
# ordinary START in GS_TITLE and enters GM_MENU; mnmain.c accepts main-menu
# VS then VS-menu Melee and enters GM_VS/GS_CSS.  The runner arms the
# read-only hardware breakpoint at HSD_GObj_80390CFC's return, 0x80390eb4.
SCENE_TITLE={SCENE_TITLE}
SCENE_MENU={SCENE_MENU}
SCENE_CSS={SCENE_CSS}
SCENE_SSS={SCENE_SSS}
SCENE_MEMCARD={SCENE_MEMCARD}
SCENE_OPENING_MOVIE={SCENE_OPENING_MOVIE}
SCHEDULER_RETURN=0x{SCHEDULER_RETURN:08x}
COLD_BOOT_TICK_LIMIT=7200
CARD_PROMPT_TICK_LIMIT=1800
CARD_PROMPT_PULSE_TICKS=32
OPENING_MOVIE_TICK_LIMIT=7200
SSS_ENTRY_TICK_LIMIT=180
SSS_CURSOR_PROC=0x8025A310

def _cold_boot_scene_name(kind):
    return {{SCENE_TITLE:'GS_TITLE',SCENE_MENU:'GS_MENU',SCENE_CSS:'GS_CSS',
            SCENE_SSS:'GS_SSS',SCENE_MEMCARD:'GS_MEMCARD',
            SCENE_OPENING_MOVIE:'GS_MOVIE_OPENING'}}.get(kind, f'GS_{{kind:#x}}')

def _cold_boot_wait_neutral(count):
    # A stopped scheduler continues at the collector's pinned return
    # breakpoint.  No source memory or register write is performed here.
    for _ in range(count):
        step(1)

def _cold_boot_card_prompt():
    # GS_MEMCARD is the source's actual card-prompt scene.  A is sent only
    # through the ordinary PAD pipe; the prompt's source callback decides
    # whether it is accepted.  The scene must leave within this bound.
    prompt_ticks=0
    while prompt_ticks < CARD_PROMPT_TICK_LIMIT:
        if scene_kind()!=SCENE_MEMCARD:
            return
        pulse(0,'A',settle=30)
        # pulse(A) advances 2 ticks while held and 30 ticks after release.
        prompt_ticks+=CARD_PROMPT_PULSE_TICKS
    raise RuntimeError('cold boot GS_MEMCARD prompt did not resolve within the bound')

def _cold_boot_wait_menu(cur,limit=2400):
    # A fresh title START can route through another source card prompt while
    # GM_MENU is loading.  Keep that prompt handling in the actual scene loop;
    # do not let wait_menu mistake a card scene for a stalled menu animation.
    for _ in range(limit):
        kind=scene_kind()
        if kind==SCENE_MEMCARD:
            _cold_boot_card_prompt()
            continue
        if kind==SCENE_MENU:
            return wait_menu(cur)
        step(1)
    raise RuntimeError(f'cold boot menu did not reach {{cur}}: scene={{_cold_boot_scene_name(scene_kind())}}')

def _cold_boot_sss_cursor():
    # mnStageSel_Scene_OnEnter starts the stage selector with a 20-tick
    # input cooldown.  The cursor is created by the source process
    # fn_8025A310; observing both source markers avoids sending the inherited
    # B route during the SSS construction window.
    cursor=None
    g=u32(u32(0x804D782C)+20)
    seen=set()
    while g and g not in seen and len(seen)<100:
        seen.add(g)
        proc=u32(g+24);pseen=set()
        while proc and proc not in pseen and len(pseen)<100:
            pseen.add(proc)
            if u32(proc+20)==SSS_CURSOR_PROC:
                if cursor is not None:
                    raise RuntimeError('ambiguous source SSS cursor process')
                cursor=u32(g+40)
            proc=u32(proc)
        g=u32(g+8)
    return cursor

def _cold_boot_wait_sss_ready():
    if scene_kind()!=SCENE_SSS:
        raise RuntimeError(f'SSS readiness entered in scene {{_cold_boot_scene_name(scene_kind())}}')
    for _ in range(SSS_ENTRY_TICK_LIMIT):
        cursor=_cold_boot_sss_cursor()
        cooldown=u32(0x804D6CA4)
        # 0x804D6CA4 is mnStageSel's source-owned entry/input cooldown.  The
        # 0x804D6BC8 word is mnmain's unrelated MenuInputState cooldown; it
        # can retain the CSS/main-menu value (for example 5) after SSS entry.
        if cursor is not None and cooldown==0:
            return
        step(1)
        if scene_kind()!=SCENE_SSS:
            raise RuntimeError(f'SSS construction left source scene: {{_cold_boot_scene_name(scene_kind())}}')
    raise RuntimeError(f'original SSS did not expose cursor/cooldown readiness: cursor={{_cold_boot_sss_cursor()}} cooldown={{u32(0x804D6CA4)}}')

def _cold_boot_check_character_availability():
    # CSS itself is the source-owned availability oracle.  Check every
    # requested icon before moving a cursor so a missing persistent unlock is
    # reported as a bounded setup prerequisite, rather than as a misleading
    # cursor-selection failure halfway through a multi-player route.
    icons=[mem(0x803F0B24+i*28,28) for i in range(25)]
    missing=[]
    for player in EXPECTED_PLAYERS:
        kind=player['character_kind']
        if sum(1 for row in icons if row[1]==kind and row[2]>=1)!=1:
            missing.append(kind)
    if missing:
        raise RuntimeError(f'cold CSS source availability missing character kinds {{missing}}; owned card baseline must already contain the unlock state')

def cold_boot_to_css():
    """Drive a fresh retail boot to source CSS using ordinary PAD input."""
    observed=[]
    last=None
    scene_ticks=0
    for _ in range(COLD_BOOT_TICK_LIMIT):
        kind=scene_kind()
        if kind!=last:
            observed.append(kind)
            print('Cold boot scene',_cold_boot_scene_name(kind),flush=True)
            last=kind
            scene_ticks=0
        scene_ticks+=1
        if kind==SCENE_MEMCARD:
            _cold_boot_card_prompt()
            continue
        if kind==SCENE_OPENING_MOVIE:
            # The source opening-mode state has no menu callback; let the
            # authored movie advance to its pinned title state.  Do not
            # infer a title from elapsed time or inject a skip flag.
            if scene_ticks>OPENING_MOVIE_TICK_LIMIT:
                raise RuntimeError('cold boot opening movie exceeded its bound')
            _cold_boot_wait_neutral(1)
            continue
        if kind==SCENE_TITLE:
            # gmTitleMode's actual exit path tests PAD_START.  Wait for the
            # source scene to be live before publishing that one input.
            _cold_boot_wait_neutral(4)
            pulse(0,'START',settle=30)
            continue
        if kind==SCENE_MENU:
            # Readiness is the original MenuFlow state and cooldown, not a
            # guessed number of frames after the scene-kind transition.
            _cold_boot_wait_menu(0)
            move_menu_selection(1)  # SEL_MAIN_VS in pinned mnmain.c
            pulse(0,'A',settle=24)
            _cold_boot_wait_menu(2) # MENU_KIND_VS
            pulse(0,'A',settle=30)  # SEL_VS_MELEE -> GM_VS
            continue
        if kind==SCENE_CSS:
            # The existing preparation body starts from this source CSS and
            # returns to the original SSS boundary after editing the rules.
            print('Cold boot reached original CSS',flush=True)
            return
        if kind==SCENE_SSS:
            raise RuntimeError('cold boot reached SSS before source CSS preparation')
        _cold_boot_wait_neutral(1)
    names=','.join(_cold_boot_scene_name(kind) for kind in observed)
    raise RuntimeError(f'cold boot did not reach original CSS within bound; scenes={{names}}')

def _cold_boot_enter_sss():
    # The existing rules routine intentionally starts at SSS. Establish
    # that source precondition through CSS first; all cursor and costume
    # changes below use the original source-driven helpers. Apply any declared
    # CPU slots through the same CSS controls used by the full preparation.
    _cold_boot_check_character_availability()
    expected_css=[(p['character_kind'],p['costume'],p['player_type']) for p in EXPECTED_PLAYERS]
    for port,player in enumerate(EXPECTED_PLAYERS):
        select(port,player['character_kind'])
        set_costume(port,player['costume'])
    for door,player in enumerate(EXPECTED_PLAYERS):
        if player['player_type']:
            set_cpu_mode(door)
            set_cpu_level(door,int(player['cpu_level']))
    players=[css_player(i) for i in range(len(EXPECTED_PLAYERS))]
    if EXPECTED.get('teams_enabled',False):
        _cold_boot_configure_teams()
        players=[css_player(i) for i in range(len(EXPECTED_PLAYERS))]
    if [(p['character_kind'],p['costume'],p['slot_type']) for p in players] != expected_css:
        raise RuntimeError(f'cold CSS roster setup mismatch: expected {{expected_css}}, got {{players}}')
    if [p['team'] for p in players] != EXPECTED_TEAMS:
        raise RuntimeError(f'cold CSS team setup mismatch: expected {{EXPECTED_TEAMS}}, got {{players}}')
    for _ in range(120):
        if mem(0x804D6CF2,1)==b'\0' and mem(0x804D6CF7,1)!=b'\0':
            break
        step(1)
    else:
        raise RuntimeError('cold CSS did not become ready for source Start')
    command(0,'PRESS START');step(1);command(0,'RELEASE START');step(1)
    for _ in range(600):
        kind=scene_kind()
        if kind==SCENE_SSS:
            print('Cold boot reached original SSS',flush=True)
            _cold_boot_wait_sss_ready()
            return
        if kind==SCENE_MEMCARD:
            _cold_boot_card_prompt()
            continue
        if kind not in (SCENE_CSS,):
            raise RuntimeError(f'cold CSS Start reached unexpected source scene {{_cold_boot_scene_name(kind)}}')
        step(1)
    raise RuntimeError('cold CSS Start did not reach original SSS within the bound')

def cold_boot_to_sss():
    """Drive a fresh retail boot through CSS to the SSS precondition."""
    cold_boot_to_css()
    _cold_boot_enter_sss()
'''


_MENU_ROUTE_TRACE_HELPER = r'''
# Read-only source-frame snapshots for a fresh boot/title/main-menu/CSS
# round trip. Controller intent stays in the separate command log above.
MENU_ROUTE_TRACE_NAME='cold-boot-menu-route.jsonl'
MENU_ROUTE_TRACE_PATH=Path(os.environ['MELEE_REPLAY_REFERENCE_WORK'])/MENU_ROUTE_TRACE_NAME
MENU_ROUTE_TRACE_PATH.parent.mkdir(parents=True,exist_ok=True)
MENU_ROUTE_TRACE=MENU_ROUTE_TRACE_PATH.open('x',encoding='utf-8')
MENU_ROUTE_TRACE_SEQUENCE=0
MENU_ROUTE_TRACE_LIMIT=12000
GM_TITLE=0
GM_MENU=1
GM_VS=2
SCENE_VS=2
SCENE_RESULTS=5
VS_START_DATA_ADDRESS=0x80480530
VS_RESULTS_DATA_ADDRESS=0x8047C020

def _menu_route_sound_balance():
    # Source layout: gmm_x0.thing begins at 0x1898 and Sound balance is
    # SaveData byte 0x45C within that object (gmm_x1868.x1CB0.sound_balance).
    base=u32(0x804D3EE0)
    offset=0x1898+0x45C
    if not 0x80000000<=base<=0x81800000-offset-1:
        raise RuntimeError(f'invalid source gmm_x0 owner for Sound observation: {base:#x}')
    return mem(base+offset,1)[0]


def _menu_route_snapshot(event):
    kind=scene_kind()
    row={'event':event,'scene_kind':kind,
         'game_mode':mem(0x80479D30,1)[0],
         'scene_frame':u32(0x80479D58),
         'pad_copy_status_hex':mem(0x804C20BC,68).hex()}
    rng_pointer=u32(0x804D5F94)
    if 0x80000000<=rng_pointer<=0x81800000-4:
        row['rng']={'pointer':hex(rng_pointer),'value':u32(rng_pointer)}
    else:
        row['rng']=None
    if kind==SCENE_MENU:
        menu_flow=mem(0x804A04F0,0x18)
        row['menu_flow_hex']=menu_flow.hex()
        row['menu_state']={'cur':menu_flow[0],'prev':menu_flow[1],
                           'hovered':struct.unpack_from('>H',menu_flow,2)[0],
                           'confirmed':menu_flow[4],'entering':menu_flow[0x11]}
        row['menu_input_cooldown']=u32(0x804D6BC8)
        row['sound_balance']={'save_data_offset':'0x45C',
                              'value':_menu_route_sound_balance()}
        row['rules_state']=rules_state()
        if row['menu_state']['cur']==0x10:
            row['item_input_locked']=mem(0x804D6BEC,1)[0]
    elif kind==SCENE_CSS:
        pointer=u32(0x804D6CB0)
        if not 0x80000000<=pointer<=0x81800000-0x100:
            raise RuntimeError('invalid original CSS data owner during route capture')
        css_data=mem(pointer+0x10,0xF0)
        row['css_data_hex']=css_data.hex()
        row['css_setup']={'is_teams':css_data[8],
                          'player_teams':[css_data[0x69],css_data[0x8D]]}
        cursors=[]
        for port in range(4):
            cursor_pointer=u32(0x804A0BC0+port*4)
            model_pointer=u32(0x804A0BD0+port*4)
            if cursor_pointer==0 and model_pointer==0:
                cursors.append(None)
                continue
            if ((cursor_pointer and not 0x80000000<=cursor_pointer<=0x81800000-20) or
                    (model_pointer and not 0x80000000<=model_pointer<=0x81800000-24)):
                raise RuntimeError(f'invalid source CSS cursor/model owner for port {port+1}')
            if not cursor_pointer or not model_pointer:
                cursors.append({'port':port+1,'cursor_present':bool(cursor_pointer),
                                'model_present':bool(model_pointer)})
                continue
            cursor=mem(cursor_pointer,20)
            model=mem(model_pointer,24)
            icon_index=mem(0x803F0DFC+port*36+14,1)[0]
            selected=-1
            if icon_index<25:
                selected=mem(0x803F0B24+icon_index*28+1,1)[0]
            cursors.append({'port':cursor[4],'cursor_state':cursor[5],
                            'cursor_target':cursor[6],
                            'held':cursor[6] if cursor[5]==1 and cursor[6]<4 else -1,
                            'selected':selected,'model_owner':model[5],
                            'cursor':struct.unpack_from('>ff',cursor,12),
                            'model':struct.unpack_from('>ff',model,8)})
        row['css_cursors']=cursors
    if kind in (SCENE_CSS,SCENE_SSS,SCENE_VS):
        row['rules_state']=rules_state()
    if kind==SCENE_SSS:
        row['selected_stage_kind']=selected_stage_kind()
    elif kind==SCENE_VS:
        row['match_start_data']=_vs_start_data_state()
    elif kind==SCENE_RESULTS:
        row['results_outcome']=mem(VS_RESULTS_DATA_ADDRESS+0x0C,1)[0]
        row['results_frame_count']=u32(VS_RESULTS_DATA_ADDRESS+0x10)
    hps=mem(0x803BB300,0x40).split(b'\0',1)[0]
    row['current_hps_hex']=hps.hex()
    row['hps_voice_word']=hex(u32(0x804D6038))
    if kind==SCENE_TITLE:
        row['sound_balance']={'save_data_offset':'0x45C',
                              'value':_menu_route_sound_balance()}
    return row

def _record_menu_route_frame():
    global MENU_ROUTE_TRACE_SEQUENCE
    if MENU_ROUTE_TRACE_SEQUENCE>=MENU_ROUTE_TRACE_LIMIT:
        raise RuntimeError('cold menu route source-frame trace budget exhausted')
    row=_menu_route_snapshot('scheduler_return')
    row['sequence']=MENU_ROUTE_TRACE_SEQUENCE
    MENU_ROUTE_TRACE.write(json.dumps(row,separators=(',',':'))+'\n')
    MENU_ROUTE_TRACE.flush()
    MENU_ROUTE_TRACE_SEQUENCE+=1

def _record_menu_route_marker(name):
    row=_menu_route_snapshot(name)
    row['sequence']=MENU_ROUTE_TRACE_SEQUENCE
    MENU_ROUTE_TRACE.write(json.dumps(row,separators=(',',':'))+'\n')
    MENU_ROUTE_TRACE.flush()
'''


_COLD_BOOT_MENU_ROUTE_HELPERS = r'''
def _cold_boot_wait_main_menu(menu_kind=0,limit=1800):
    for _ in range(limit):
        kind=scene_kind()
        mode=mem(0x80479D30,1)[0]
        if kind==SCENE_MEMCARD:
            _cold_boot_card_prompt()
            continue
        if kind==SCENE_MENU and mode==GM_MENU:
            return _cold_boot_wait_menu(menu_kind,limit=limit)
        step(1)
    raise RuntimeError(f'original GM_MENU did not reach the root menu: scene={_cold_boot_scene_name(scene_kind())} mode={mem(0x80479D30,1)[0]}')

def _cold_boot_wait_css(limit=900):
    for _ in range(limit):
        kind=scene_kind()
        mode=mem(0x80479D30,1)[0]
        if kind==SCENE_MEMCARD:
            _cold_boot_card_prompt()
            continue
        if kind==SCENE_CSS and mode==GM_VS:
            _cold_boot_check_character_availability()
            return
        if kind not in (SCENE_MENU,SCENE_CSS):
            raise RuntimeError(f'VS selection reached unexpected source scene {_cold_boot_scene_name(kind)} mode={mode}')
        step(1)
    raise RuntimeError(f'original GM_VS did not reach CSS: scene={_cold_boot_scene_name(scene_kind())} mode={mem(0x80479D30,1)[0]}')

def _cold_boot_css_to_title():
    if scene_kind()!=SCENE_CSS or mem(0x80479D30,1)[0]!=GM_VS:
        raise RuntimeError('CSS-to-title route requires live original GM_VS/GS_CSS')
    for port in range(4):
        command(port,'SET MAIN .5 .5')
    step(12)
    # Probe ordinary B first. In source, B is handled by the CSS character
    # doors, while the scene-level parent return is the L+R+Start chord.
    # Preserve both source snapshots so any CSS-local effect is visible in
    # the capture without mistaking it for a parent-scene transition.
    _record_menu_route_marker('css_before_b_back_probe')
    pulse(0,'B',settle=24)
    if scene_kind()!=SCENE_CSS or mem(0x80479D30,1)[0]!=GM_VS:
        raise RuntimeError(f'ordinary CSS B left the CSS unexpectedly: scene={_cold_boot_scene_name(scene_kind())} mode={mem(0x80479D30,1)[0]}')
    _record_menu_route_marker('css_b_back_probe_remained_css')
    # mnCharSel's retail parent-menu shortcut is L+R+Start. Keep the chord
    # source-owned; do not write the pending game-mode field directly.
    command(0,'PRESS L');command(0,'PRESS R');step(4)
    command(0,'PRESS START');step(12)
    command(0,'RELEASE L');command(0,'RELEASE R');command(0,'RELEASE START')
    # GM_MENU inherits the VS submenu from GM_VS. Retail's CSS parent-menu
    # shortcut therefore lands at MENU_KIND_VS (2), not the root menu (0).
    state=_cold_boot_wait_main_menu(2)
    if state['cur']!=2 or state['hovered']!=0:
        raise RuntimeError(f'CSS back route did not reach the VS submenu at Melee: {state}')
    _record_menu_route_marker('versus_submenu_ready_after_css')
    # B is the ordinary submenu Back input. The first B returns to the root
    # menu; the next B from the root requests GM_TITLE.
    pulse(0,'B',settle=24)
    state=_cold_boot_wait_main_menu(0)
    if state['cur']!=0 or state['hovered']!=1:
        raise RuntimeError(f'VS submenu Back did not return to root at Versus: {state}')
    _record_menu_route_marker('root_main_menu_ready')
    # PAD_CANCEL in the root menu requests GM_TITLE.
    pulse(0,'B',settle=30)
    for _ in range(900):
        kind=scene_kind()
        mode=mem(0x80479D30,1)[0]
        if kind==SCENE_TITLE and mode==GM_TITLE:
            _record_menu_route_marker('title_ready')
            _cold_boot_wait_neutral(24)
            return
        if kind==SCENE_MEMCARD:
            _cold_boot_card_prompt()
            continue
        step(1)
    raise RuntimeError(f'root-menu B did not reach original title: scene={_cold_boot_scene_name(scene_kind())} mode={mem(0x80479D30,1)[0]}')

def _cold_boot_title_to_css():
    if scene_kind()!=SCENE_TITLE or mem(0x80479D30,1)[0]!=GM_TITLE:
        raise RuntimeError('title-to-CSS route requires live original GM_TITLE/GS_TITLE')
    # The title callback has a source-owned 20-tick input cooldown. Let it
    # expire before sending one ordinary Start edge.
    _cold_boot_wait_neutral(24)
    pulse(0,'START',settle=30)
    for _ in range(1800):
        kind=scene_kind()
        mode=mem(0x80479D30,1)[0]
        if kind==SCENE_MEMCARD:
            _cold_boot_card_prompt()
            continue
        if kind==SCENE_MENU and mode==GM_MENU:
            state=_cold_boot_wait_menu(0)
            if state['cur']!=0 or state['hovered']!=0:
                raise RuntimeError(f'title Start did not open the default 1P root menu: {state}')
            _record_menu_route_marker('root_main_menu_ready_after_title')
            move_menu_selection(1)  # SEL_MAIN_VS
            pulse(0,'A',settle=24)
            state=_cold_boot_wait_menu(2)  # MENU_KIND_VS
            if state['cur']!=2 or state['hovered']!=0:
                raise RuntimeError(f'root menu Versus did not open at Melee: {state}')
            _record_menu_route_marker('versus_submenu_ready_after_title')
            pulse(0,'A',settle=30)  # SEL_VS_MELEE -> GM_VS
            _cold_boot_wait_css()
            _record_menu_route_marker('round_trip_css_ready')
            return
        if kind not in (SCENE_TITLE,SCENE_OPENING_MOVIE):
            raise RuntimeError(f'title Start reached unexpected source scene {_cold_boot_scene_name(kind)} mode={mode}')
        step(1)
    raise RuntimeError(f'title Start did not reach original GM_MENU: scene={_cold_boot_scene_name(scene_kind())} mode={mem(0x80479D30,1)[0]}')

def cold_boot_css_menu_round_trip():
    """Capture a continuous cold boot, CSS -> root menu -> title -> CSS route."""
    _record_menu_route_marker('first_scheduler_return')
    cold_boot_to_css()
    _record_menu_route_marker('cold_css_ready')
    _cold_boot_css_to_title()
    _cold_boot_title_to_css()

def cold_boot_css_sound_settings_route():
    """Capture retail Main > Settings > Sound, edit it, and navigate back."""
    _record_menu_route_marker('first_scheduler_return')
    cold_boot_to_css()
    _record_menu_route_marker('cold_css_ready')
    _cold_boot_css_to_title()

    if scene_kind()!=SCENE_TITLE or mem(0x80479D30,1)[0]!=GM_TITLE:
        raise RuntimeError('Sound route requires live original GM_TITLE/GS_TITLE')
    _cold_boot_wait_neutral(24)
    pulse(0,'START',settle=30)
    state=_cold_boot_wait_main_menu(0)
    if state['cur']!=0 or state['hovered']!=0:
        raise RuntimeError(f'title Start did not open the root menu: {state}')
    _record_menu_route_marker('root_main_menu_ready_after_title')
    move_menu_selection(3)  # SEL_MAIN_SETTINGS in the pinned mnmain.c
    state=menu_state()
    if state['cur']!=0 or state['hovered']!=3:
        raise RuntimeError(f'root menu did not select Settings: {state}')
    _record_menu_route_marker('main_settings_selected')
    pulse(0,'A',settle=24)
    state=wait_menu(4)
    if state['hovered']!=0:
        raise RuntimeError(f'Settings did not open at Rumble: {state}')
    _record_menu_route_marker('settings_ready')
    move_menu_selection(1)  # SEL_SETTINGS_SOUND
    state=menu_state()
    if state['cur']!=4 or state['hovered']!=1:
        raise RuntimeError(f'Settings did not select Sound: {state}')
    _record_menu_route_marker('settings_sound_selected')
    pulse(0,'A',settle=24)
    state=wait_menu(20)
    if state['hovered']!=0:
        raise RuntimeError(f'Sound menu did not open at its default row: {state}')
    before=_menu_route_sound_balance()
    if before!=0:
        raise RuntimeError(f'fresh-card Sound balance was not centered: {before}')
    _record_menu_route_marker('sound_screen_ready')

    # Back from Sound is a cancellation: the source value remains centered,
    # and the Settings callback retains its Sound selection on re-entry.
    pulse(0,'B',settle=24)
    state=wait_menu(4)
    if state['hovered']!=1 or _menu_route_sound_balance()!=before:
        raise RuntimeError(f'Sound Back changed state or selection: {state}')
    _record_menu_route_marker('settings_after_sound_cancel')
    pulse(0,'A',settle=24)
    state=wait_menu(20)
    if state['hovered']!=0 or _menu_route_sound_balance()!=before:
        raise RuntimeError(f'Sound re-entry did not preserve the unedited value: {state}')
    _record_menu_route_marker('sound_screen_reentered')
    pulse(0,'D_DOWN')
    pulse(0,'D_LEFT')
    after=_menu_route_sound_balance()
    if after!=251:
        raise RuntimeError(f'Sound Left did not change source balance from 0 to -5: {after}')
    _record_menu_route_marker('sound_balance_after_change')

    pulse(0,'B',settle=24)
    state=wait_menu(4)
    if state['hovered']!=1 or _menu_route_sound_balance()!=after:
        raise RuntimeError(f'Sound edit did not return to Settings with state retained: {state}')
    _record_menu_route_marker('settings_after_sound_change')
    pulse(0,'B',settle=24)
    state=wait_menu(0)
    if state['hovered']!=3:
        raise RuntimeError(f'Settings Back did not retain Main Settings selection: {state}')
    _record_menu_route_marker('main_after_settings')
    pulse(0,'B',settle=30)
    for _ in range(900):
        if scene_kind()==SCENE_TITLE and mem(0x80479D30,1)[0]==GM_TITLE:
            _record_menu_route_marker('title_after_sound')
            return
        if scene_kind()==SCENE_MEMCARD:
            _cold_boot_card_prompt()
            continue
        step(1)
    raise RuntimeError(f'Main Back did not exit Sound route to Title: scene={_cold_boot_scene_name(scene_kind())} mode={mem(0x80479D30,1)[0]}')
'''


_VS_RULES_ITEMS_ROUTE_HELPERS = r'''
def _css_team_setup_state():
    pointer=u32(0x804D6CB0)
    if not 0x80000000<=pointer<=0x81800000-0x100:
        raise RuntimeError('invalid original CSSData owner during team setup')
    raw=mem(pointer+0x10,0xF0)
    return {'is_teams':raw[8],
            'player_teams':[raw[0x69],raw[0x8D]]}

def _cold_boot_configure_teams():
    before=_css_team_setup_state()
    if before['is_teams']!=0 or before['player_teams']!=[0,0]:
        raise RuntimeError(f'fresh CSS team defaults differ from the declared source setup: {before}')

    move_cursor(0,-30.0,23.3)
    _record_menu_route_marker('teams_toggle_before')
    pulse(0,'A',settle=24)
    toggled=_css_team_setup_state()
    if toggled['is_teams']!=1:
        raise RuntimeError(f'original CSS Teams control did not enable team rules: {toggled}')
    _record_menu_route_marker('teams_toggle_after')

    door_raw=mem(0x803F0DFC+0x24,0x24)
    left,right=struct.unpack_from('>ff',door_raw,0x1C)
    if not left<right:
        raise RuntimeError(f'original P2 team-color bounds are invalid: {left}/{right}')
    move_cursor(0,(left+right)/2,-3.4)
    before_color=_css_team_setup_state()
    if before_color['player_teams']!=[0,0]:
        raise RuntimeError(f'CSS Teams toggle unexpectedly changed player colors: {before_color}')
    _record_menu_route_marker('p2_team_color_before')
    pulse(0,'A',settle=24)
    after_color=_css_team_setup_state()
    if after_color!={'is_teams':1,'player_teams':[0,1]}:
        raise RuntimeError(f'original P2 team-color control did not select team 1: {after_color}')
    _record_menu_route_marker('p2_team_color_after')

def _vs_start_data_state():
    # Source identities from mn/types.h: StartMeleeRules is 0x60 bytes,
    # followed by six 0x24-byte PlayerInitData rows. In StartMeleeRules,
    # timer_enabled is the seventh MSB-first u32 bitfield and time_limit is
    # the big-endian u32 at 0x10. gmVsMelee_StartData is pinned in the Rev. 2
    # symbol map at 0x80480530.
    raw=mem(VS_START_DATA_ADDRESS,0x138)
    players=[]
    for index in range(6):
        offset=0x60+index*0x24
        players.append({'character_kind':struct.unpack('b',raw[offset:offset+1])[0],
                        'slot_type':raw[offset+1],
                        'stocks':struct.unpack('b',raw[offset+2:offset+3])[0],
                        'color':raw[offset+3],
                        'team':raw[offset+9],
                        'rumble_enabled':raw[offset+12]&1,
                        'cpu_kind':raw[offset+14],
                        'cpu_level':raw[offset+15]})
    return {'is_teams':raw[0x08],
            'timer_enabled':bool(raw[0]&0x02),
            'time_limit_seconds':struct.unpack('>I',raw[0x10:0x14])[0],
            'item_frequency':struct.unpack('b',raw[0x15:0x16])[0],
            'stage':struct.unpack('>H',raw[0x18:0x1A])[0],
            'item_mask_hex':f'{int.from_bytes(raw[0x30:0x38],"big"):016x}',
            'players':players}

def _vs_wait_scene(target_scene,target_mode,limit=1800):
    for _ in range(limit):
        if (scene_kind()==target_scene and
                mem(0x80479D30,1)[0]==target_mode):
            return
        if scene_kind()==SCENE_MEMCARD:
            _cold_boot_card_prompt()
            continue
        step(1)
    raise RuntimeError(f'original VS route did not reach scene/mode {target_scene}/{target_mode}: scene={scene_kind()} mode={mem(0x80479D30,1)[0]}')

def _vs_rules_css_to_parent_menu():
    if scene_kind()!=SCENE_CSS or mem(0x80479D30,1)[0]!=GM_VS:
        raise RuntimeError('VS Rules route requires live original GM_VS/GS_CSS')
    for port in range(4): command(port,'SET MAIN .5 .5')
    step(12)
    command(0,'PRESS L');command(0,'PRESS R');step(4)
    command(0,'PRESS START');step(12)
    command(0,'RELEASE L');command(0,'RELEASE R');command(0,'RELEASE START')
    state=_cold_boot_wait_main_menu(2)
    if state['cur']!=2 or state['hovered']!=0:
        raise RuntimeError(f'CSS parent-menu route did not reach the VS submenu: {state}')
    _record_menu_route_marker('versus_submenu_after_css_parent')

def _vs_rules_wait_menu(menu_kind,hovered=None,limit=2400):
    for _ in range(limit):
        if scene_kind()==SCENE_MENU and mem(0x80479D30,1)[0]==GM_MENU:
            state=menu_state()
            if (state['cur']==menu_kind and
                    (hovered is None or state['hovered']==hovered) and
                    u32(0x804D6BC8)==0):
                return state
        step(1)
    raise RuntimeError(f'VS Rules route did not reach menu {menu_kind}/{hovered}: {menu_state()}')

def _vs_rules_wait_items(limit=2400):
    for _ in range(limit):
        if scene_kind()==SCENE_MENU and mem(0x80479D30,1)[0]==GM_MENU:
            state=menu_state()
            if (state['cur']==0x10 and mem(0x804D6BEC,1)[0]==0 and
                    u32(0x804D6BC8)==0):
                return state
        step(1)
    raise RuntimeError(f'original Items input lock did not clear: {menu_state()} lock={mem(0x804D6BEC,1)[0]}')

def _vs_items_move_cursor(target,limit=64):
    state=menu_state()
    if state['cur']!=0x10:
        raise RuntimeError(f'Items cursor move left the source Items menu: {state}')
    if state['hovered']==target:
        return state
    # mnItemSw_80233B68 maps D-left at item cell 0 directly to frequency row
    # 31. This route deliberately enters Items at source row zero and uses
    # that authored wrap edge instead of treating the grid as a linear list.
    if target!=0x1F or state['hovered']!=0:
        raise RuntimeError(f'route requires the original item-grid 0 -> frequency-row wrap: {state} -> {target}')
    pulse(0,'D_LEFT')
    state=_vs_rules_wait_items()
    if state['hovered']!=target:
        raise RuntimeError(f'original Items D-left did not reach frequency row {target}: {state}')
    return state

def _cold_boot_vs_rules_items_round_trip():
    """Capture cold-DOL Rules/Items, match, No Contest Results and CSS return."""
    _record_menu_route_marker('first_scheduler_return')
    cold_boot_to_css()
    _record_menu_route_marker('cold_css_ready')
    _vs_rules_css_to_parent_menu()
    pulse(0,'B',settle=24)
    state=_vs_rules_wait_menu(0,1)
    _record_menu_route_marker('root_menu_after_vs_back')

    move_menu_selection(1)  # SEL_MAIN_VS
    pulse(0,'A',settle=24)
    state=_vs_rules_wait_menu(2,0)
    _record_menu_route_marker('versus_submenu_for_rules')
    move_menu_selection(3)  # SEL_VS_RULES
    pulse(0,'A',settle=30)
    state=_vs_rules_wait_menu(13,0)
    _record_menu_route_marker('vs_rules_first_entry')

    rules_before=rules_state()
    move_menu_selection(5,limit=24)
    pulse(0,'A',settle=30)
    _vs_rules_wait_items()
    items_before=rules_state()
    _record_menu_route_marker('vs_items_entry')
    if not 0<=menu_state()['confirmed']<=1:
        raise RuntimeError(f'original Items first-row selection is out of range: {menu_state()}')
    pulse(0,'A',settle=12)
    item_toggle=rules_state()
    changed_bits=items_before['item_mask'] ^ item_toggle['item_mask']
    if changed_bits==0 or changed_bits & (changed_bits-1):
        raise RuntimeError(f'original Items A did not change exactly one source mask bit: before={items_before} after={item_toggle}')
    _record_menu_route_marker('vs_items_one_bit_toggled')

    state=_vs_items_move_cursor(0x1F)
    if not 0<=state['confirmed']<=5:
        raise RuntimeError(f'original item frequency selector is out of range: {state}')
    for _ in range(6):
        if menu_state()['confirmed']==0:
            break
        pulse(0,'D_UP')
        _vs_rules_wait_items()
    if menu_state()['confirmed']!=0:
        raise RuntimeError(f'original item-frequency selector did not reach None: {menu_state()}')
    _record_menu_route_marker('vs_items_frequency_none')

    pulse(0,'B',settle=30)
    _vs_rules_wait_menu(13,5)
    items_committed=rules_state()
    if (items_committed['item_frequency']!=-1 or
            items_committed['item_mask']!=item_toggle['item_mask']):
        raise RuntimeError(f'original Items B did not commit item mask/None frequency: {items_committed}')
    _record_menu_route_marker('vs_items_back_committed')
    pulse(0,'B',settle=30)
    _vs_rules_wait_menu(2,3)
    _record_menu_route_marker('vs_rules_back_to_versus')
    pulse(0,'B',settle=30)
    _vs_rules_wait_menu(0,1)
    _record_menu_route_marker('versus_back_to_main')

    move_menu_selection(1)
    pulse(0,'A',settle=24)
    _vs_rules_wait_menu(2,0)
    move_menu_selection(3)
    pulse(0,'A',settle=30)
    _vs_rules_wait_menu(13,0)
    retained_before_stock=rules_state()
    if (retained_before_stock['item_frequency']!=-1 or
            retained_before_stock['item_mask']!=item_toggle['item_mask']):
        raise RuntimeError(f'Rules re-entry did not retain original item preferences: {retained_before_stock}')
    _record_menu_route_marker('vs_rules_reentry_retained_items')

    move_menu_selection(1)
    for _ in range(100):
        state=menu_state()
        if state['hovered']!=1:
            raise RuntimeError(f'Rules stock edit changed its source row: {state}')
        if state['confirmed']==3:
            break
        pulse(0,'D_LEFT' if state['confirmed']>3 else 'D_RIGHT')
    else:
        raise RuntimeError(f'original Rules stock selector did not reach three: {menu_state()}')
    _record_menu_route_marker('vs_rules_stock_three_selected')

    # Rules row 6 opens the authored Rules Plus screen (MenuKind 15). Change
    # the stock timer through its row-zero D-pad controls, back out to Rules,
    # and re-enter before accepting it so both retention and Start are sourced
    # from the retail menu callbacks.
    move_menu_selection(6)
    if menu_state()['cur']!=13 or menu_state()['hovered']!=6:
        raise RuntimeError(f'Rules Plus entry did not use original Rules row 6: {menu_state()}')
    pulse(0,'A',settle=30)
    rules_plus=_vs_rules_wait_menu(15,0)
    timer_before=rules_plus['confirmed']
    if not 0<=timer_before<=99 or rules_state()['stock_time_limit']!=timer_before:
        raise RuntimeError(f'Rules Plus did not expose its authored stock-timer value: menu={rules_plus} rules={rules_state()}')
    _record_menu_route_marker('vs_rules_plus_entry')
    if timer_before==1:
        pulse(0,'D_LEFT')
        rules_plus=_vs_rules_wait_menu(15,0)
        if rules_plus['confirmed']!=0 or rules_state()['stock_time_limit']!=0:
            raise RuntimeError(f'Rules Plus left input did not change one minute to zero: {rules_plus} {rules_state()}')
    for _ in range(100):
        rules_plus=menu_state()
        if rules_plus['confirmed']==1:
            break
        pulse(0,'D_LEFT' if rules_plus['confirmed']>1 else 'D_RIGHT')
        _vs_rules_wait_menu(15,0)
    else:
        raise RuntimeError(f'original Rules Plus timer selector did not reach one minute: {menu_state()}')
    if rules_state()['stock_time_limit']!=1:
        raise RuntimeError(f'Rules Plus one-minute value did not reach source GameRules: {rules_state()}')
    _record_menu_route_marker('vs_rules_plus_timer_one')
    pulse(0,'B',settle=30)
    rules_after_plus_back=_vs_rules_wait_menu(13,6)
    if rules_state()['stock_time_limit']!=1:
        raise RuntimeError(f'Rules Plus B did not commit one minute before returning to Rules: {rules_after_plus_back} {rules_state()}')
    _record_menu_route_marker('vs_rules_plus_back_retained')
    pulse(0,'A',settle=30)
    rules_plus_reentry=_vs_rules_wait_menu(15,0)
    if rules_plus_reentry['confirmed']!=1 or rules_state()['stock_time_limit']!=1:
        raise RuntimeError(f'Rules Plus re-entry did not retain the committed one-minute timer: {rules_plus_reentry} {rules_state()}')
    _record_menu_route_marker('vs_rules_plus_reentry_retained')

    pulse(0,'START',settle=12)
    _cold_boot_wait_css()
    final_rules=rules_state()
    if (final_rules['stock_count']!=3 or final_rules['stock_time_limit']!=1 or
            final_rules['item_frequency']!=-1 or
            final_rules['item_mask']!=item_toggle['item_mask']):
        raise RuntimeError(f'GM_VS CSS handoff did not retain source Rules/Items/Rules Plus values: {final_rules}')
    _record_menu_route_marker('css_after_rules_start_retained')

    # Continue through original CSS and SSS to a real source VS match. The
    # preparation helpers steer CSS/SSS geometry but leave character, stage,
    # StartMeleeData, and match ownership with their retail callbacks.
    _cold_boot_enter_sss()
    if scene_kind()!=SCENE_SSS or mem(0x80479D30,1)[0]!=GM_VS:
        raise RuntimeError('original Rules route did not enter GM_VS/GS_SSS')
    _record_menu_route_marker('sss_after_rules_start')
    select_stage(EXPECTED_STAGE)
    if selected_stage_kind()!=EXPECTED_STAGE:
        raise RuntimeError(f'original SSS cursor did not select Final Destination: {selected_stage_kind()}')
    _record_menu_route_marker('sss_final_destination_selected')
    pulse(0,'A',settle=30)
    _vs_wait_scene(SCENE_VS,GM_VS)
    start_data=_vs_start_data_state()
    if (start_data['stage']!=EXPECTED_STAGE or
            start_data['item_frequency']!=-1 or
            start_data['item_mask_hex']!=f"{item_toggle['item_mask']:016x}" or
            len(start_data['players'])!=6 or
            [(p['character_kind'],p['slot_type'],p['stocks'],p['color'],p['team'])
             for p in start_data['players'][:len(EXPECTED_PLAYERS)]] !=
            [(int(p['character_kind']),int(p['player_type']),3,int(p['costume']),int(p.get('team',0)))
             for p in EXPECTED_PLAYERS] or
            any(p['slot_type']!=3 for p in start_data['players'][len(EXPECTED_PLAYERS):])):
        raise RuntimeError(f'original SSS handoff did not carry source Rules/Items into GM_VS: {start_data}')
    _record_menu_route_marker('vs_match_entered')
    step(180)
    if scene_kind()!=SCENE_VS or mem(0x80479D30,1)[0]!=GM_VS:
        raise RuntimeError('original VS match did not remain live for the declared 180 source scheduler ticks')
    _record_menu_route_marker('vs_match_after_180_ticks')

    # The source no-contest chord is one simultaneous P1 PAD sample. It is
    # processed by gm_16AE.c and enters the ordinary Results mode route.
    for button in ('L','R','A','START'):
        command(0,'PRESS '+button)
    step(1)
    for button in ('L','R','A','START'):
        command(0,'RELEASE '+button)
    step(1)
    _record_menu_route_marker('vs_no_contest_chord_sent')
    _vs_wait_scene(SCENE_RESULTS,GM_VS)
    result_outcome=mem(VS_RESULTS_DATA_ADDRESS+0x0C,1)[0]
    if result_outcome!=7:
        raise RuntimeError(f'original No Contest did not produce OUTCOME_NO_CONTEST=7: {result_outcome}')
    _record_menu_route_marker('results_no_contest')
    # Results' source player route accepts P1 Start after its presentation
    # delay. Retry only after checking the current scene; no result state is
    # skipped or written.
    step(270)
    for _ in range(8):
        if scene_kind()==SCENE_CSS and mem(0x80479D30,1)[0]==GM_VS:
            break
        if scene_kind()!=SCENE_RESULTS or mem(0x80479D30,1)[0]!=GM_VS:
            raise RuntimeError(f'original Results left its checked owner unexpectedly: scene={scene_kind()} mode={mem(0x80479D30,1)[0]}')
        pulse(0,'START',settle=90)
    if scene_kind()!=SCENE_CSS or mem(0x80479D30,1)[0]!=GM_VS:
        raise RuntimeError('original Results Start did not return through the source VS route to CSS')
    retained_after_results=rules_state()
    if (retained_after_results['stock_count']!=3 or
            retained_after_results['stock_time_limit']!=1 or
            retained_after_results['item_frequency']!=-1 or
            retained_after_results['item_mask']!=item_toggle['item_mask']):
        raise RuntimeError(f'Results/CSS return did not retain source Rules/Items/Rules Plus settings: {retained_after_results}')
    _record_menu_route_marker('css_after_results_retained')
'''


_COMMAND_LOG_HELPER = rf'''
# Every controller command is retained as reproducible provenance.  The log
# records the source scene/frame observed immediately before the pipe write;
# it does not capture or recreate game state.
COMMAND_LOG_NAME={COMMAND_LOG_NAME!r}
COMMAND_LOG_PATH=Path(os.environ['MELEE_REPLAY_REFERENCE_WORK'])/COMMAND_LOG_NAME
COMMAND_LOG_PATH.parent.mkdir(parents=True,exist_ok=True)
COMMAND_LOG=COMMAND_LOG_PATH.open('x',encoding='utf-8')
def _record_input_command(port,text):
    row={{'event':'pad_command','port':port+1,'command':text,
         'scene_kind':scene_kind(),'game_mode':mem(0x80479D30,1)[0],
         'scene_frame':u32(0x80479D58),
         'source_sequence':globals().get('MENU_ROUTE_TRACE_SEQUENCE')}}
    if row['scene_kind']==SCENE_MENU:
        menu_flow=mem(0x804A04F0,0x18)
        row['menu_state']={{'cur':menu_flow[0],'prev':menu_flow[1],
                           'hovered':struct.unpack_from('>H',menu_flow,2)[0],
                           'confirmed':menu_flow[4],'entering':menu_flow[0x11]}}
    COMMAND_LOG.write(json.dumps(row,separators=(',',':'))+'\n')
    COMMAND_LOG.flush()
'''


_ALLOCATION_FAILURE_CHECK = r'''
def _check_collector_failure():
    # reference_allocation_capture.py shares this GDB globals dictionary.  Its
    # static-call observers set _allocation_failed before returning True; the
    # menu route must stop at the very next scheduler boundary rather than
    # continuing into a partial setup or emitting a misleading ready record.
    failure=globals().get('_allocation_failed')
    if failure:
        raise RuntimeError('allocation observer failed during menu preparation: '+str(failure))
'''


def _compose_driver(*, menu_round_trip: bool = False,
                    vs_rules_items_round_trip: bool = False,
                    menu_sound_route: bool = False) -> str:
    """Compose a cold route around the existing source-menu body."""
    if sum((menu_round_trip, vs_rules_items_round_trip, menu_sound_route)) > 1:
        raise ValueError("choose one original menu route capture")
    marker = "\n# Preparation only, before a new checkpoint."
    if _BASE_DRIVER_SOURCE.count(marker) != 1:
        raise ValueError("retail CPU menu driver preparation marker changed")
    source = _BASE_DRIVER_SOURCE.replace(marker, _COMMAND_LOG_HELPER + marker, 1)
    source = source.replace(
        "    _write_state(port)\ndef step(count):",
        "    _record_input_command(port,text)\n    _write_state(port)\ndef step(count):",
        1,
    )
    source = source.replace(
        "def step(count):",
        _ALLOCATION_FAILURE_CHECK + "\ndef step(count):",
        1,
    )
    source = source.replace(
        "        gdb.execute('continue',to_string=True)\n        current=u32(0x80479D58)",
        "        gdb.execute('continue',to_string=True)\n        _check_collector_failure()\n        current=u32(0x80479D58)",
        1,
    )
    route_helpers = _COLD_BOOT_HELPERS
    if menu_round_trip or vs_rules_items_round_trip or menu_sound_route:
        if source.count("completed+=1;previous=current") != 1:
            raise ValueError("retail CPU menu source-frame boundary changed")
        source = source.replace(
            "completed+=1;previous=current",
            "completed+=1;previous=current\n        _record_menu_route_frame()",
            1,
        )
        trace_helper = _MENU_ROUTE_TRACE_HELPER
        route_helpers += _COLD_BOOT_MENU_ROUTE_HELPERS
        if vs_rules_items_round_trip:
            trace_helper = trace_helper.replace(MENU_ROUTE_TRACE_NAME,
                                                VS_RULES_ITEMS_TRACE_NAME)
            route_helpers += _VS_RULES_ITEMS_ROUTE_HELPERS
        source = source.replace(marker, trace_helper + marker, 1)
    source = source.replace(marker, route_helpers + marker, 1)
    route = ("cold_boot_css_menu_round_trip" if menu_round_trip else
             "_cold_boot_vs_rules_items_round_trip" if vs_rules_items_round_trip else
             "cold_boot_css_sound_settings_route" if menu_sound_route else
             "cold_boot_to_sss")
    route_note = (
        "# Fresh-DOL route through the retail parent menus and title, then CSS.\n"
        if menu_round_trip else
        "# Fresh-DOL VS Rules/Items -> SSS -> source match -> Results -> CSS route.\n"
        if vs_rules_items_round_trip else
        "# Cold boot through CSS, return through Main to Title, edit Sound, "
        "then leave through Main to Title using original menu callbacks.\n"
        if menu_sound_route else
        "# Fresh-DOL entry is complete only when source CSS/SSS is observed.\n"
        "# The existing rules routine starts from SSS; cold_boot_to_sss()\n"
        "# establishes that source boundary through ordinary CSS input.\n"
    )
    source = source.replace(
        marker,
        "\n" + route_note + route + "()\n" + marker,
        1,
    )
    if menu_round_trip or vs_rules_items_round_trip or menu_sound_route:
        # This driver is a route capture, not the separate CSS/SSS setup
        # recipe appended by the base module. Keep the retained script bounded
        # at its declared original menu or Results/CSS endpoint.
        source = source.split(marker, 1)[0]
    return source


DRIVER_SOURCE = _compose_driver()
MENU_ROUND_TRIP_DRIVER_SOURCE = _compose_driver(menu_round_trip=True)
VS_RULES_ITEMS_ROUND_TRIP_DRIVER_SOURCE = _compose_driver(
    vs_rules_items_round_trip=True)
MENU_SOUND_ROUTE_DRIVER_SOURCE = _compose_driver(menu_sound_route=True)


def render_cold_boot_driver() -> str:
    """Return the self-contained fresh-DOL source-menu driver."""
    return DRIVER_SOURCE


def render_menu_round_trip_driver() -> str:
    """Return a fresh-DOL CSS -> parent menus -> title -> CSS driver."""
    return MENU_ROUND_TRIP_DRIVER_SOURCE


def render_vs_rules_items_round_trip_driver() -> str:
    """Return a fresh-DOL Rules/Items -> match -> Results -> CSS route."""
    return VS_RULES_ITEMS_ROUND_TRIP_DRIVER_SOURCE


def render_menu_sound_route_driver() -> str:
    """Return a fresh-DOL Main > Settings > Sound route driver."""
    return MENU_SOUND_ROUTE_DRIVER_SOURCE


# Compatibility aliases used by collector/preparation builders.
render_source_driver = render_cold_boot_driver
render_driver = render_cold_boot_driver


def write_driver(path: str | Path, *, menu_round_trip: bool = False,
                 vs_rules_items_round_trip: bool = False,
                 menu_sound_route: bool = False) -> Path:
    """Write one cold-boot driver copy and return its path."""
    if sum((menu_round_trip, vs_rules_items_round_trip, menu_sound_route)) > 1:
        raise ValueError("choose one original menu route capture")
    destination = Path(path)
    source = (MENU_ROUND_TRIP_DRIVER_SOURCE if menu_round_trip else
              VS_RULES_ITEMS_ROUND_TRIP_DRIVER_SOURCE if vs_rules_items_round_trip else
              MENU_SOUND_ROUTE_DRIVER_SOURCE if menu_sound_route else
              DRIVER_SOURCE)
    destination.write_text(source, encoding="utf-8")
    return destination


def scheduler_breakpoint_command() -> str:
    """Return the pinned read-only scheduler breakpoint command."""
    return f"hbreak *0x{SCHEDULER_RETURN:08x}"


def validate_driver() -> None:
    """Reject source writes, state fabrication, or unbounded cold routing."""
    if "write_memory(" in DRIVER_SOURCE:
        raise ValueError("cold-boot source menu driver must not write game memory")
    if "put_register" in DRIVER_SOURCE or "set $" in DRIVER_SOURCE:
        raise ValueError("cold-boot source menu driver must not write registers")
    if "load_state" in DRIVER_SOURCE.lower() or "savestate" in DRIVER_SOURCE.lower():
        raise ValueError("cold-boot source menu driver must not load a saved state")
    if "gdb.execute('continue'" not in DRIVER_SOURCE:
        raise ValueError("cold-boot source menu driver must use scheduler continuation")
    if "SCHEDULER_RETURN=0x80390eb4" not in DRIVER_SOURCE:
        raise ValueError("cold-boot driver lost the pinned scheduler boundary")
    if "COLD_BOOT_TICK_LIMIT=7200" not in DRIVER_SOURCE:
        raise ValueError("cold-boot route must retain an explicit bound")
    if COMMAND_LOG_NAME not in DRIVER_SOURCE:
        raise ValueError("cold-boot route must retain its input command log")
    compile(DRIVER_SOURCE, "retail_allocation_menu.gdb.py", "exec")

    round_trip = MENU_ROUND_TRIP_DRIVER_SOURCE
    compile(round_trip, "retail_menu_round_trip.gdb.py", "exec")
    if "write_memory(" in round_trip or "put_register" in round_trip:
        raise ValueError("menu round-trip driver must not write source state")
    if "load_state" in round_trip.lower() or "savestate" in round_trip.lower():
        raise ValueError("menu round-trip driver must not load a saved state")
    if MENU_ROUTE_TRACE_NAME not in round_trip:
        raise ValueError("menu round-trip driver must retain source-frame state")
    if "_cold_boot_css_to_title()" not in round_trip or "_cold_boot_title_to_css()" not in round_trip:
        raise ValueError("menu round-trip driver lost an original back/forward route")
    if "pulse(0,'B',settle=30)" not in round_trip:
        raise ValueError("menu round-trip driver must use the original root-menu Back input")
    if "PRESS L');command(0,'PRESS R'" not in round_trip:
        raise ValueError("menu round-trip driver lost the CSS parent-menu chord")

    rules_items = VS_RULES_ITEMS_ROUND_TRIP_DRIVER_SOURCE
    compile(rules_items, "retail_vs_rules_items_round_trip.gdb.py", "exec")
    if "write_memory(" in rules_items or "put_register" in rules_items:
        raise ValueError("VS Rules/Items route must not write source state")
    if "_cold_boot_vs_rules_items_round_trip()" not in rules_items:
        raise ValueError("VS Rules/Items route lost its cold-DOL entry point")
    for marker in ("vs_rules_first_entry", "vs_items_entry",
                   "vs_items_back_committed", "vs_rules_back_to_versus",
                   "vs_rules_stock_three_selected",
                   "css_after_rules_start_retained", "sss_final_destination_selected",
                   "vs_match_after_180_ticks", "vs_no_contest_chord_sent",
                   "results_no_contest", "css_after_results_retained"):
        if marker not in rules_items:
            raise ValueError("VS Rules/Items route lost source marker " + marker)
    for identity in ("VS_START_DATA_ADDRESS=0x80480530",
                     "VS_RESULTS_DATA_ADDRESS=0x8047C020",
                     "_vs_start_data_state()", "_vs_wait_scene(SCENE_RESULTS,GM_VS)"):
        if identity not in rules_items:
            raise ValueError("VS Rules/Items route lost its source match boundary " + identity)
    if VS_RULES_ITEMS_TRACE_NAME not in rules_items:
        raise ValueError("VS Rules/Items route lost its separate source trace")
    if "_cold_boot_vs_rules_items_round_trip()" not in rules_items.split("# Preparation only", 1)[0]:
        raise ValueError("VS Rules/Items source route is not called before setup preparation")

    sound_route = MENU_SOUND_ROUTE_DRIVER_SOURCE
    compile(sound_route, "retail_menu_sound_route.gdb.py", "exec")
    if "write_memory(" in sound_route or "put_register" in sound_route:
        raise ValueError("Sound route driver must not write source state")
    if "load_state" in sound_route.lower() or "savestate" in sound_route.lower():
        raise ValueError("Sound route driver must not load a saved state")
    if "cold_boot_css_sound_settings_route()" not in sound_route:
        raise ValueError("Sound route driver lost its retail route callback")
    if "pulse(0,'B',settle=24)" not in sound_route or "pulse(0,'D_LEFT')" not in sound_route:
        raise ValueError("Sound route driver lost ordinary Back/edit controller inputs")
    if "0x1898+0x45C" not in sound_route:
        raise ValueError("Sound route observer lost its source SaveData offset derivation")


validate_driver()
