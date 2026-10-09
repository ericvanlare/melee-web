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
