"""Predeclared reduced original menu experiment; no settings/gameplay admission."""
from authored_sd_reference_plan import recipe, recipe_sha256
from retail_input_plan import NEUTRAL_PAD, pipe_commands
from reference_versus_sequence_capture import raw_pad

SCHEMA = "melee-web-sd-original-menu-inputs"


def guard(kind, row, value=None):
    result = {"scene": 1, "kind": kind, "row": row, "cooldown": 0, "entering": 0}
    if value is not None:
        result["value"] = value
    return result


def rules_ready_packet():
    """Source/retained raw-Pipe route, ending before changing any Rules value."""
    steps = [("main-down-to-VS", "D_DOWN", guard(0, 0), guard(0, 1)),
             ("main-select-VS", "A", guard(0, 1), guard(2, 0))]
    steps += [(f"VS-Rules-row-{row}", "D_DOWN", guard(2, row - 1), guard(2, row))
              for row in (1, 2, 3)]
    steps += [("open-Rules", "A", guard(2, 3), guard(13, 0, 0))]
    return {"schema": SCHEMA, "version": 2, "scope": "rules_ready",
            "authored_recipe_sha256": recipe_sha256(recipe(3)),
            "boot": [{"scene": scene, "p1": raw_pad(buttons=[button]), "p2": NEUTRAL_PAD}
                     for scene, button in ((0x1c, "A"), (0, "START"), (0x27, "A"))],
            "boot_max_actions": 20, "boot_max_polls": 2700,
            "actions": [{"label": label, "p1": raw_pad(buttons=[button]), "p2": NEUTRAL_PAD,
                         "before": before, "after": after, "max_polls": 600}
                        for label, button, before, after in steps],
            "stop": guard(13, 0, 0)}


def validate_packet(value):
    # Only this reduced route is supported. No guessed/default continuation.
    from authored_sd_reference_plan import canonical
    if canonical(value) == canonical(gci_competitive_entry_packet()):
        return value
    if canonical(value) == canonical(gci_sparse_pair_packet()):
        return value
    if canonical(value) == canonical(sheik_transform_prefix_packet()):
        return value
    if canonical(value) == canonical(gci_items_row_packet()):
        return value
    if canonical(value) == canonical(gci_sd_prefix_packet()):
        return value
    if canonical(value) == canonical(gci_sd_prefix_packet(7)):
        return value
    if any(canonical(value) == canonical(gci_rules_ready_packet(version)) for version in (3, 4)):
        return value
    if canonical(value) != canonical(rules_ready_packet()):
        raise ValueError("Unsupported or changed bounded Rules-ready menu packet")
    for action in value["boot"] + value["actions"]:
        pipe_commands(action["p1"])
        pipe_commands(action["p2"])
    return value


def gci_rules_ready_packet(version=4):
    """Distinct profile campaign; card confirmation is deliberately undeclared."""
    from sd_gci_profile import GCI_SHA256
    value = rules_ready_packet()
    if type(version) is not int or version not in (3, 4):
        raise ValueError("Unsupported GCI menu packet version")
    value.update(version=version, scope="rules_ready_gci", profile_gci_sha256=GCI_SHA256,
                 authored_recipe_sha256=recipe_sha256(recipe(4)))
    if version == 4:
        # Forward/back direction survives the animation cooldown. Original
        # Main/VS Confirm sets it to one; Rules construction does not clear it.
        for action in value["actions"]:
            for boundary in (action["before"], action["after"]):
                if boundary["kind"] != 0:
                    boundary["entering"] = 1
        value["stop"]["entering"] = 1
    return value


def matches(state, predicate):
    return state is not None and all(state.get(key) == value for key, value in predicate.items())


def gci_sd_prefix_packet(version=5):
    """Finite original-menu policy, authored before observing the new campaign.

    MenuFlow already exposes committed/pending values. CSS uses the existing
    source cursor/door/live-state slices. SSS needs its separate acceptance
    cooldown, not another cursor-coordinate reader. Every movement is capped.
    """
    if type(version) is not int or version not in (5,7):
        raise ValueError("Unsupported full SD menu packet version")
    value = gci_rules_ready_packet()
    value.update(version=version, scope="sd_prefix_gci",
                 authored_recipe_sha256=recipe_sha256(recipe(5)))
    def state(kind, row, confirmed=None, entering=1):
        result = guard(kind, row, confirmed)
        result["entering"] = entering
        return result
    steps = [("stock-mode", "D_RIGHT", state(13, 0, 0), state(13, 0, 1)),
             ("stock-row", "D_DOWN", state(13, 0, 1), state(13, 1, 3)),
             ("four-stocks", "D_RIGHT", state(13, 1, 3), state(13, 1, 4))]
    steps += [(f"Rules-row-{row}", "D_DOWN", state(13, row-1), state(13, row))
              for row in range(2, 7)]
    steps += [("extra-rules", "A", state(13, 6), state(15, 0, 0)),
              ("one-minute-stock-timer", "D_RIGHT", state(15, 0, 0), state(15, 0, 1)),
              ("commit-extra", "B", state(15, 0, 1), state(13, 6, entering=0)),
              ("items-row", "D_UP", state(13, 6, entering=0), state(13, 5, entering=0)),
              ("open-items", "A", state(13, 5, entering=0), state(16, 0, 1)),
              ("items-frequency-row", "D_UP", state(16, 0, 1), state(16, 31, 3))]
    steps += [(f"items-frequency-{after}", "D_RIGHT", state(16, 31, after+1), state(16, 31, after))
              for after in (2, 1, 0)]
    steps += [("commit-items-none", "B", state(16, 31, 0), state(13, 5, entering=0)),
              ("Rules-start-CSS", "START", state(13, 5, entering=0), {"scene": 8})]
    value["actions"] += [{"label": label, "p1": raw_pad(buttons=[button]), "p2": NEUTRAL_PAD,
                          "before": before, "after": after, "max_polls": 600}
                         for label, button, before, after in steps]
    value["css"] = {"character": 8, "icon": 1, "point": [-20.9, 16.5],
        "costumes": [1, 0], "ports": [0, 1], "human_kind": 0,
        "axis_values": [-70, -35, 0, 35, 70], "tolerance": 0.6,
        "stable_cursor_polls": 2, "max_move_polls": 600,
        "max_costume_taps": 8, "idle_polls_before_start": 12}
    value["sss"] = {"stage_kind": 32, "initial_idle_polls": 120,
        "column_x": 40, "column_polls": 18, "scan_y": 40,
        "max_scan_polls": 600, "confirm_requires_cooldown": 0}
    value["stop"] = "observed sd_setup; interrupted primary with complete native MWRI"
    if version == 7:
        for action in value["actions"]:
            if action["label"].startswith("items-frequency-"):
                action["before"]["items_locked"] = 0
                action["after"]["items_locked"] = 0
            if action["label"] == "commit-items-none":
                action["before"]["items_locked"] = 0
    return value


def route_pads(packet):
    """Finite PAD alphabet for the declared conditional cursor policy."""
    pads = {(NEUTRAL_PAD, NEUTRAL_PAD)} | {
        (a["p1"], a["p2"]) for a in packet["boot"] + packet["actions"]}
    if packet["scope"] == "items_row_gci":
        return pads
    for port in (0, 1):
        for button in ("A", "X"):
            pair = [NEUTRAL_PAD] * 2
            pair[port] = raw_pad(buttons=[button])
            pads.add(tuple(pair))
        for x in packet["css"]["axis_values"]:
            for y in packet["css"]["axis_values"]:
                pair = [NEUTRAL_PAD] * 2
                pair[port] = raw_pad(x=x, y=y)
                pads.add(tuple(pair))
    pads |= {(raw_pad(x=40), NEUTRAL_PAD), (raw_pad(y=40), NEUTRAL_PAD)}
    return pads


def sheik_transform_prefix_packet():
    """Exact original-menu route for Zelda/Mario's bounded Sheik prefix.

    This reuses the original Rules-ready route and declares its stock/Items
    choreography, while using dense P1/P2 ownership and no GCI injection.
    Zelda's authored icon-table row (15) is distinct from its HUD icon value
    (18); the frozen cursor point is the strict-interior midpoint of row 15.
    """
    value = rules_ready_packet()
    value.update(version=10, scope="sheik_transform_prefix",
                 authored_recipe_sha256=recipe_sha256(recipe(8)))
    # The existing v4 rules route observes forward navigation through each
    # original menu owner. Reuse those exact guards without its save injection.
    for action in value["actions"]:
        for boundary in (action["before"], action["after"]):
            if boundary["kind"] != 0:
                boundary["entering"] = 1
    def state(kind, row, confirmed=None, entering=1):
        result = guard(kind, row, confirmed)
        result["entering"] = entering
        return result
    steps = [("stock-mode", "D_RIGHT", state(13, 0, 0), state(13, 0, 1)),
             ("stock-row", "D_DOWN", state(13, 0, 1), state(13, 1, 3)),
             ("four-stocks", "D_RIGHT", state(13, 1, 3), state(13, 1, 4))]
    steps += [(f"Rules-row-{row}", "D_DOWN", state(13, row - 1), state(13, row))
              for row in range(2, 6)]
    steps += [("open-items", "A", state(13, 5), state(16, 0, 1))]
    steps += [("items-frequency-row", "D_UP", state(16, 0, 1), state(16, 31, 3))]
    steps += [(f"items-frequency-{after}", "D_RIGHT", state(16, 31, after + 1),
               state(16, 31, after)) for after in (2, 1, 0)]
    steps += [("commit-items-none", "B", state(16, 31, 0), state(13, 5, entering=0)),
              ("Rules-start-CSS", "START", state(13, 5, entering=0), {"scene": 8})]
    value["actions"] += [{"label": label, "p1": raw_pad(buttons=[button]),
                          "p2": NEUTRAL_PAD, "before": before, "after": after,
                          "max_polls": 600}
                         for label, button, before, after in steps]
    value["css"] = {
        "characters": [18, 8],
        "icon_table_indices": [15, 1],
        "hud_icons": [18, 1],
        "points": [[14.1, 9.5], [-20.9, 16.5]],
        "geometry_source": {
            "dependency_commit": "b43912cc78606f96c9569f5d6229bc9d7e265ea5",
            "file": "src/melee/mn/mncharsel.c",
            "sha256": "7d8ab4fd55c5904bb05065a5b1e57b00693998d2446894d9b0b6a695d1cb77de",
            "rows": [
                {"character_kind": 18, "icon_table_ordinal": 15,
                 "row_address": "803f0cc8", "hud_icon": 18,
                 "bounds": {"x": [10.6, 17.6], "y": [6.0, 13.0]},
                 "point": [14.1, 9.5]},
                {"character_kind": 8, "icon_table_ordinal": 1,
                 "row_address": "803f0b40", "hud_icon": 1,
                 "bounds": {"x": [-24.4, -17.4], "y": [13.0, 20.0]},
                 "point": [-20.9, 16.5]},
            ],
            "hit_test": "strict interior of the source ICONBNDS/ICONROWHT bounds",
        },
        "costumes": [1, 0], "ports": [0, 1], "source_slots": [0, 1],
        "human_kind": 0, "axis_values": [-70, -35, 0, 35, 70],
        "tolerance": 0.6, "stable_cursor_polls": 2,
        "max_move_polls": 600, "max_costume_taps": 8,
        "idle_polls_before_start": 12,
    }
    value["sss"] = {"stage_kind": 32, "initial_idle_polls": 120,
        "column_x": 40, "column_polls": 18, "scan_y": 40,
        "max_scan_polls": 600, "confirm_requires_cooldown": 0}
    # Items lock is an SD/GCI observer owner. The transform profile still
    # observes original MenuFlow values and consumed PADs, so this route does
    # not make lock-tag presence part of its readiness predicate.
    for action in value["actions"]:
        for boundary in (action.get("before"), action.get("after")):
            if isinstance(boundary, dict):
                boundary.pop("items_locked", None)
    value["stop"] = (
        "observed exact Zelda/Mario four-stock FD setup and completed grounded neutral Sheik prefix")
    return value


def gci_items_row_packet():
    """Reduced Items acceptance probe, ending after exactly one authored Up."""
    value = gci_sd_prefix_packet()
    value.update(version=6, scope="items_row_gci")
    last = next(i for i, a in enumerate(value["actions"])
                if a["label"] == "items-frequency-row")
    value["actions"] = value["actions"][:last + 1]
    action = value["actions"][-1]
    action["before"]["items_locked"] = 0
    action["after"]["items_locked"] = 0
    value.pop("css")
    value.pop("sss")
    value["stop"] = dict(action["after"])
    return value


def gci_competitive_entry_packet():
    """Original two-column Items path; each switch is observed, never inferred.

    mnItemSw_80233B68: Down 0..15, Right 15->30, Up 30..16,
    Up 16->32. Both frequency rows use x21; Right decrements its value.
    Conditional A clears only an observed on switch, preserving unmapped bit28.
    """
    value = gci_sd_prefix_packet(7)
    value.update(version=8, scope="competitive_entry_gci",
                 authored_recipe_sha256=recipe_sha256(recipe(6)))
    def state(kind, row, confirmed=None, entering=1, locked=False):
        result = guard(kind, row, confirmed)
        result["entering"] = entering
        if locked:
            result["items_locked"] = 0
        return result
    actions = value["actions"][:6]  # unchanged cold Main -> Rules route
    def action(label, button, before, after, **extra):
        actions.append(dict(label=label, p1=raw_pad(buttons=[button]), p2=NEUTRAL_PAD,
                            before=before, after=after, max_polls=600, **extra))
    action("stock-mode", "D_RIGHT", state(13,0,0), state(13,0,1))
    action("stock-row", "D_DOWN", state(13,0,1), state(13,1,3))
    action("four-stocks", "D_RIGHT", state(13,1,3), state(13,1,4))
    for row in range(2,7):
        action(f"Rules-row-{row}", "D_DOWN", state(13,row-1),
               state(13,row,0 if row == 2 else 10 if row == 3 else None))
    action("extra-rules", "A", state(13,6), state(15,0,0))
    for minute in range(1,9):
        action(f"stock-timer-{minute}", "D_RIGHT", state(15,0,minute-1), state(15,0,minute))
    action("friendly-fire-row", "D_DOWN", state(15,0,8), state(15,1,0))
    action("friendly-fire-on", "D_RIGHT", state(15,1,0), state(15,1,1))
    action("pause-row", "D_DOWN", state(15,1,1), state(15,2,1))
    action("pause-off", "D_RIGHT", state(15,2,1), state(15,2,0))
    action("commit-extra", "B", state(15,2,0), state(13,6,entering=0))
    action("items-row", "D_UP", state(13,6,entering=0), state(13,5,entering=0))
    action("open-items", "A", state(13,5,entering=0), state(16,0,1))
    path = list(range(16)) + list(range(30,15,-1))
    for index, row in enumerate(path):
        action(f"item-{row}-off", "A", state(16,row,locked=True), state(16,row,0,locked=True),
               when_value=1)
        target = path[index+1] if index+1 < len(path) else 32
        button = "D_DOWN" if row < 15 else "D_RIGHT" if row == 15 else "D_UP"
        action(f"item-{row}-to-{target}", button, state(16,row,0,locked=True),
               state(16,target,3 if target == 32 else None,locked=True))
    for after in (2,1,0):
        action(f"items-frequency-{after}", "D_RIGHT", state(16,32,after+1,locked=True),
               state(16,32,after,locked=True))
    action("commit-items-none", "B", state(16,32,0,locked=True), state(13,5,entering=0))
    action("Rules-start-CSS", "START", state(13,5,entering=0), {"scene":8})
    value["actions"] = actions
    value["items"] = {"rows":path, "preference_bits":[5,18,10,30,13,24,3,14,23,27,1,9,8,7,21,4,
        6,2,15,0,17,11,31,26,20,25,16,22,19,29,12], "frequency_row":32,
        "unmapped_bit":28, "expected_preference_mask_hex":"0000000010000000"}
    value["stop"] = "observed vs_setup; interrupted primary with complete native MWRI"
    return value


SPARSE_LOADED_RULES_HEX = "0036000203000a00000001000000080800000800ffffffff"


def gci_sparse_pair_packet():
    """Default Rules route into a strict original source-slot pair 0/2 probe."""
    value = gci_rules_ready_packet()
    value.update(version=9, scope="sparse_pair_gci",
                 authored_recipe_sha256=recipe_sha256(recipe(7)))
    # Reuse the verified stock/frequency choreography, omitting only the
    # extra-Rules one-minute timer detour. The saved stock timer stays zero.
    source = gci_sd_prefix_packet(7)
    omitted = {"Rules-row-6", "extra-rules", "one-minute-stock-timer",
               "commit-extra", "items-row"}
    value["actions"] = [action for action in source["actions"]
                        if action["label"] not in omitted]
    for action in value["actions"]:
        if action["label"] == "open-items":
            action["before"]["entering"] = 1
    value["css"] = {"character": 8, "icon": 1, "point": [-20.9, 16.5],
        "costumes": [1, 0], "ports": [0, 1], "source_slots": [0, 2],
        "human_kind": 0, "axis_values": [-70, -35, 0, 35, 70],
        "tolerance": 0.6, "stable_cursor_polls": 2,
        "max_move_polls": 600, "max_costume_taps": 8,
        "idle_polls_before_start": 12}
    value["sss"] = {"stage_kind": 32, "initial_idle_polls": 120,
        "column_x": 40, "column_polls": 18, "scan_y": 40,
        "max_scan_polls": 600, "confirm_requires_cooldown": 0}
    value["stop"] = ("observed exact VS setup, distinct source PAD0/PAD2 consume and release; "
                      "interrupted before terminal")
    return value
