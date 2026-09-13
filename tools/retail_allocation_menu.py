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
    # The existing rules routine intentionally starts at SSS.  Establish
    # that source precondition through CSS first; all cursor and costume
    # changes below use the existing source-driven helpers. Keep every slot
    # human until the reused preparation body applies CPU modes.
    _cold_boot_check_character_availability()
    expected_css=[(p['character_kind'],p['costume'],0) for p in EXPECTED_PLAYERS]
    for port,player in enumerate(EXPECTED_PLAYERS):
        select(port,player['character_kind'])
        set_costume(port,player['costume'])
    players=[css_player(i) for i in range(len(EXPECTED_PLAYERS))]
    if [(p['character_kind'],p['costume'],p['slot_type']) for p in players] != [(k,c,0) for k,c,_ in expected_css]:
        raise RuntimeError(f'cold CSS selection mismatch: expected {{expected_css}}, got {{players}}')
    players=[css_player(i) for i in range(len(EXPECTED_PLAYERS))]
    if [(p['character_kind'],p['costume'],p['slot_type']) for p in players] != expected_css:
        raise RuntimeError(f'cold CSS human setup mismatch: expected {{expected_css}}, got {{players}}')
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
         'scene_kind':scene_kind(),'scene_frame':u32(0x80479D58)}}
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


def _compose_driver() -> str:
    """Compose the cold route around the existing source-menu body."""
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
    source = source.replace(marker, _COLD_BOOT_HELPERS + marker, 1)
    source = source.replace(
        marker,
        "\n# Fresh-DOL entry is complete only when source CSS/SSS is observed.\n"
        "# The existing rules routine starts from SSS; cold_boot_to_sss()\n"
        "# establishes that source boundary through ordinary CSS input.\n"
        "cold_boot_to_sss()\n" + marker,
        1,
    )
    return source


DRIVER_SOURCE = _compose_driver()


def render_cold_boot_driver() -> str:
    """Return the self-contained fresh-DOL source-menu driver."""
    return DRIVER_SOURCE


# Compatibility aliases used by collector/preparation builders.
render_source_driver = render_cold_boot_driver
render_driver = render_cold_boot_driver


def write_driver(path: str | Path) -> Path:
    """Write one cold-boot driver copy and return its path."""
    destination = Path(path)
    destination.write_text(DRIVER_SOURCE, encoding="utf-8")
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


validate_driver()
