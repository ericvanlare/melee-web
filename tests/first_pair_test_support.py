"""Small test-only resolver for the project's pinned SDK Node."""
import ast
from pathlib import Path
import sys


def pinned_node_or_skip(testcase, root):
    root = Path(root)
    sdk = root / ".deps/emsdk"
    config = sdk / ".emscripten"
    version = sdk / "upstream/emscripten/emscripten-version.txt"
    if not config.is_file() or not version.is_file():
        testcase.skipTest("Pinned Emscripten SDK Node/config unavailable")

    scripts = root / "scripts"
    if str(scripts) not in sys.path:
        sys.path.insert(0, str(scripts))
    from check_gameplay import node_runtime

    try:
        return node_runtime(root)
    except FileNotFoundError as error:
        testcase.skipTest(f"Pinned Emscripten SDK Node/config unavailable: {error}")
    except ValueError as error:
        # node_runtime uses this error for both an outside-SDK path and a missing
        # configured binary. Skip only the latter; malformed/outside config fails.
        if str(error) != "Configured Node must belong to the project SDK":
            raise
        assignments = [ast.literal_eval(statement.value)
                       for statement in ast.parse(config.read_text()).body
                       if isinstance(statement, ast.Assign) and any(
                           isinstance(target, ast.Name) and target.id == "NODE_JS"
                           for target in statement.targets)]
        if len(assignments) != 1 or not isinstance(assignments[0], str):
            raise
        configured = Path(assignments[0].replace("$CFGDIR", str(sdk))).resolve()
        if configured.is_relative_to(sdk.resolve()) and not configured.is_file():
            testcase.skipTest("Pinned Emscripten SDK Node binary unavailable")
        raise
