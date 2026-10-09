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
    if canonical(value) == canonical(gci_sd_prefix_packet()):
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


def gci_sd_prefix_packet():
    """Finite original-menu policy, authored before observing the new campaign.

    MenuFlow already exposes committed/pending values. CSS uses the existing
    source cursor/door/live-state slices. SSS needs its separate acceptance
    cooldown, not another cursor-coordinate reader. Every movement is capped.
    """
    value = gci_rules_ready_packet()
    value.update(version=5, scope="sd_prefix_gci",
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
    return value


def route_pads(packet):
    """Finite PAD alphabet for the declared conditional cursor policy."""
    pads = {(NEUTRAL_PAD, NEUTRAL_PAD)} | {
        (a["p1"], a["p2"]) for a in packet["boot"] + packet["actions"]}
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
