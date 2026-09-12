// Physical QWERTY positions. B0XX reference and behavior: docs/KEYBOARD_LAYOUTS.md.
export function keyboardRows(layout, secondKeyboard = true) {
  if (layout === 'boxx') return [
    ['Move ← ↓ → ↑', '2 3 4 ]'],
    ['Attack', 'M'], ['Special', 'O'], ['Jump X / Y', 'P / 0'],
    ['Shield L / R', 'Q / 9'], ['Grab', '['],
    ['C-stick ← ↓ → ↑', 'N Space , K'],
    ['Mod X / Y', 'V / B'], ['Light / mid shield', '− / ='],
    ['D-pad', 'Arrows · or V + B + C-stick'], ['Start', '7'],
  ];
  if (layout !== 'two') throw Error('Unknown keyboard layout.');
  return [
    ['Move', 'W A S D', 'Arrows'], ['Attack', 'J', 'Right Shift'],
    ['Special', 'K', 'Right Ctrl'], ['Jump', 'U / I', 'Delete'],
    ['Shield', 'Q / E', 'Home / Page Up'], ['Grab', 'O', 'Page Down'],
    ['C-stick', 'T G F H', 'Numpad 8 2 4 6'],
    ['D-pad', secondKeyboard ? 'Z X C V' : 'Arrows', '—'],
    ['Start', 'Enter', 'End'],
  ];
}
