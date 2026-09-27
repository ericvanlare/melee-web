/** Optional touch overlay that submits through the shared browser controller PAD adapter. */
const BUTTON_INDEX = Object.freeze({A: 0, B: 1, X: 2, Y: 3, L: 4, Z: 5, triggerL: 6,
  triggerR: 7, R: 8, Start: 9, Up: 12, Down: 13, Left: 14, Right: 15});
const STICK_AXES = Object.freeze({main: [0, 1], cstick: [2, 3]});
const clamp = (value, low, high) => Math.max(low, Math.min(high, value));

export function applyTouchDeadZone(x, y, deadZone = 0.15) {
  const length = Math.hypot(x, y);
  if (!Number.isFinite(length) || length <= deadZone) return [0, 0];
  const magnitude = (Math.min(1, length) - deadZone) / (1 - deadZone);
  return [x / length * magnitude, y / length * magnitude];
}

function button(text, action, className, ariaLabel = `${text} button`) {
  const control = document.createElement('button');
  control.type = 'button';
  control.className = `touch-button ${className}`;
  control.dataset.touchButton = action;
  control.setAttribute('aria-label', ariaLabel);
  control.textContent = text;
  return control;
}

function emptyGamepad() {
  return {buttons: Array.from({length: 16}, () => ({pressed: false, value: 0})), axes: [0, 0, 0, 0]};
}

function installStyle() {
  if (document.querySelector?.('link[data-melee-touch-controls-style]')) return;
  const link = document.createElement('link');
  link.dataset.meleeTouchControlsStyle = '';
  link.rel = 'stylesheet';
  link.href = new URL('./touch-controls.css', import.meta.url).href;
  document.head?.append(link);
}

/**
 * Place the optional controls over the canvas's fitted 4:3 rectangle. All
 * buttons, axes, triggers and releases travel through setTouchGamepad at the
 * same controller sampling boundary used by physical Browser Gamepads.
 */
export function mountTouchControls({container, canvas} = {}) {
  if (!container || !canvas) throw Error('Touch controls need a game container and canvas.');
  installStyle();
  const root = container;
  const hostElement = root.parentElement || root;
  root.replaceChildren();
  root.classList.add('touch-controls');
  root.setAttribute('role', 'group');
  root.setAttribute('aria-label', 'Player 1 touch controller');
  root.setAttribute('aria-hidden', 'true');
  root.hidden = true;

  const leftShoulders = document.createElement('div');
  leftShoulders.className = 'touch-shoulders touch-shoulders-left';
  leftShoulders.append(button('L', 'L', '', 'L shoulder · full analog pressure'),
    button('Z', 'Z', '', 'Z button'));
  const rightShoulders = document.createElement('div');
  rightShoulders.className = 'touch-shoulders touch-shoulders-right';
  rightShoulders.append(button('R', 'R', '', 'R shoulder · full analog pressure'),
    button('Start', 'Start', 'touch-start', 'Start button'));

  const dpad = document.createElement('div');
  dpad.className = 'touch-dpad';
  dpad.setAttribute('role', 'group');
  dpad.setAttribute('aria-label', 'D-pad');
  dpad.append(button('↑', 'Up', 'dpad-up', 'D-pad up'), button('←', 'Left', 'dpad-left', 'D-pad left'),
    button('→', 'Right', 'dpad-right', 'D-pad right'), button('↓', 'Down', 'dpad-down', 'D-pad down'));

  function stick(name, label) {
    const control = document.createElement('div');
    control.className = `touch-stick touch-stick-${name}`;
    control.dataset.touchStick = name;
    control.setAttribute('role', 'group');
    control.setAttribute('aria-label', `${label}; drag to aim, release to center`);
    const ring = document.createElement('span');
    ring.className = 'touch-stick-ring';
    const knob = document.createElement('span');
    knob.className = 'touch-stick-knob';
    knob.textContent = name === 'main' ? 'M' : 'C';
    knob.setAttribute('aria-hidden', 'true');
    const caption = document.createElement('span');
    caption.className = 'touch-stick-caption';
    caption.textContent = label;
    control.append(ring, knob, caption);
    return control;
  }
  const mainStick = stick('main', 'Stick');
  const cStick = stick('cstick', 'C-stick');

  const face = document.createElement('div');
  face.className = 'touch-face';
  face.setAttribute('role', 'group');
  face.setAttribute('aria-label', 'A, B, X and Y buttons');
  face.append(button('X', 'X', 'face-x'), button('Y', 'Y', 'face-y'),
    button('B', 'B', 'face-b'), button('A', 'A', 'face-a'));

  root.append(leftShoulders, rightShoulders, dpad, mainStick, cStick, face);

  let controller = null, enabled = false, opacity = 0.55;
  let pointerOwners = new Map(), stickOwners = new Map(), stickValues = {main: [0, 0], cstick: [0, 0]};
  const buttons = [...root.querySelectorAll('[data-touch-button]')];
  const sticks = [...root.querySelectorAll('[data-touch-stick]')];
  const probe = document.createElement('div');
  probe.className = 'touch-safe-area-probe';
  document.body?.append(probe);
  let lastLayout = null;

  function held(action) {
    for (const owner of pointerOwners.values()) if (owner.kind === 'button' && owner.action === action) return true;
    return false;
  }
  function rawGamepad() {
    const raw = emptyGamepad();
    for (const action of ['A', 'B', 'X', 'Y', 'L', 'R', 'Z', 'Start', 'Up', 'Down', 'Left', 'Right']) {
      const pressed = held(action), index = BUTTON_INDEX[action];
      raw.buttons[index] = {pressed, value: Number(pressed)};
    }
    // Touch shoulders are digital buttons, so they report a full trigger
    // sample (255) alongside their digital L/R bit, matching a full squeeze.
    for (const [action, index] of [['L', BUTTON_INDEX.triggerL], ['R', BUTTON_INDEX.triggerR]]) {
      const pressed = held(action);
      raw.buttons[index] = {pressed, value: Number(pressed)};
    }
    for (const [name, indices] of Object.entries(STICK_AXES)) {
      const [x, y] = stickValues[name];
      raw.axes[indices[0]] = x;
      raw.axes[indices[1]] = y;
    }
    return raw;
  }
  function publish() {
    controller?.setTouchGamepad?.(enabled ? rawGamepad() : null);
    for (const control of buttons) control.classList.toggle('is-active', held(control.dataset.touchButton));
  }
  function capture(event, element) {
    event.preventDefault();
    event.stopPropagation();
    try { element.setPointerCapture(event.pointerId); } catch { /* Synthetic pointer fixtures have no active browser pointer. */ }
  }
  function release(pointerId) {
    const owner = pointerOwners.get(pointerId);
    if (!owner) return;
    pointerOwners.delete(pointerId);
    if (owner.kind === 'stick' && stickOwners.get(owner.name) === pointerId) {
      stickOwners.delete(owner.name);
      stickValues[owner.name] = [0, 0];
      owner.element.style.setProperty('--stick-x', '0px');
      owner.element.style.setProperty('--stick-y', '0px');
    }
    publish();
  }
  function releaseAll() {
    const owners = [...pointerOwners.entries()];
    pointerOwners.clear();
    stickOwners.clear();
    stickValues = {main: [0, 0], cstick: [0, 0]};
    for (const [id, owner] of owners) {
      if (owner.kind === 'stick') {
        owner.element.style.setProperty('--stick-x', '0px');
        owner.element.style.setProperty('--stick-y', '0px');
      }
      try { if (owner.element.hasPointerCapture?.(id)) owner.element.releasePointerCapture(id); } catch { /* Already canceled by the browser. */ }
    }
    publish();
  }
  function moveStick(name, element, event) {
    if (stickOwners.get(name) !== event.pointerId) return;
    const rect = element.getBoundingClientRect();
    const radius = Math.max(1, Math.min(rect.width, rect.height) * 0.42);
    // Keep the unclamped displacement until radial normalization. Clamping
    // axes independently before normalization changes the direction of an
    // off-axis drag that crosses the stick rim.
    const x = (event.clientX - (rect.left + rect.width / 2)) / radius;
    const y = (event.clientY - (rect.top + rect.height / 2)) / radius;
    const [outX, outY] = applyTouchDeadZone(x, y);
    stickValues[name] = [outX, outY];
    element.style.setProperty('--stick-x', `${outX * 34}%`);
    element.style.setProperty('--stick-y', `${outY * 34}%`);
    publish();
  }
  function primaryPointer(event) {
    return event.pointerType !== 'mouse' || event.button === 0;
  }
  for (const control of buttons) {
    const action = control.dataset.touchButton;
    control.addEventListener('pointerdown', event => {
      if (!enabled || !primaryPointer(event)) return;
      if (pointerOwners.has(event.pointerId)) release(event.pointerId);
      capture(event, control);
      pointerOwners.set(event.pointerId, {kind: 'button', action, element: control});
      publish();
    });
    for (const type of ['pointerup', 'pointercancel', 'lostpointercapture'])
      control.addEventListener(type, event => release(event.pointerId));
    control.addEventListener('click', event => event.preventDefault());
    control.addEventListener('keydown', event => {
      if (!enabled || !['Enter', ' '].includes(event.key) || event.repeat) return;
      event.preventDefault();
      const id = `key:${control.dataset.touchButton}:${event.code}`;
      pointerOwners.set(id, {kind: 'button', action, element: control});
      publish();
    });
    control.addEventListener('keyup', event => {
      if (!['Enter', ' '].includes(event.key)) return;
      event.preventDefault();
      release(`key:${control.dataset.touchButton}:${event.code}`);
    });
  }
  for (const control of sticks) {
    const name = control.dataset.touchStick;
    control.addEventListener('pointerdown', event => {
      if (!enabled || !primaryPointer(event) || stickOwners.has(name)) return;
      if (pointerOwners.has(event.pointerId)) release(event.pointerId);
      capture(event, control);
      pointerOwners.set(event.pointerId, {kind: 'stick', name, element: control});
      stickOwners.set(name, event.pointerId);
      moveStick(name, control, event);
    });
    control.addEventListener('pointermove', event => moveStick(name, control, event));
    for (const type of ['pointerup', 'pointercancel', 'lostpointercapture'])
      control.addEventListener(type, event => release(event.pointerId));
  }

  function updateLayout() {
    if (!root.isConnected) return;
    const host = hostElement.getBoundingClientRect(), rect = canvas.getBoundingClientRect();
    if (!(rect.width > 0 && rect.height > 0)) return;
    const style = getComputedStyle(probe);
    const safeLeft = Math.max(0, (parseFloat(style.paddingLeft) || 0) - rect.left);
    const safeTop = Math.max(0, (parseFloat(style.paddingTop) || 0) - rect.top);
    const safeRight = Math.max(0, rect.right - (innerWidth - (parseFloat(style.paddingRight) || 0)));
    const safeBottom = Math.max(0, rect.bottom - (innerHeight - (parseFloat(style.paddingBottom) || 0)));
    const layout = [rect.left, rect.top, rect.width, rect.height, safeLeft, safeTop, safeRight, safeBottom];
    if (lastLayout && pointerOwners.size && layout.some((value, index) => Math.abs(value - lastLayout[index]) > 0.5))
      releaseAll();
    lastLayout = layout;
    root.style.left = `${rect.left - host.left}px`;
    root.style.top = `${rect.top - host.top}px`;
    root.style.width = `${rect.width}px`;
    root.style.height = `${rect.height}px`;
    root.style.setProperty('--touch-safe-left', `${safeLeft}px`);
    root.style.setProperty('--touch-safe-top', `${safeTop}px`);
    root.style.setProperty('--touch-safe-right', `${safeRight}px`);
    root.style.setProperty('--touch-safe-bottom', `${safeBottom}px`);
  }
  const resizeObserver = typeof ResizeObserver === 'function' ? new ResizeObserver(updateLayout) : null;
  resizeObserver?.observe(hostElement);
  resizeObserver?.observe(canvas);
  window.addEventListener('resize', updateLayout);
  window.addEventListener('orientationchange', updateLayout);
  window.visualViewport?.addEventListener('resize', updateLayout);
  window.visualViewport?.addEventListener('scroll', updateLayout);
  document.addEventListener('fullscreenchange', updateLayout);
  const onBlur = () => releaseAll();
  const onVisibility = () => { if (document.hidden) releaseAll(); };
  const onPagehide = () => releaseAll();
  window.addEventListener('blur', onBlur);
  document.addEventListener('visibilitychange', onVisibility);
  window.addEventListener('pagehide', onPagehide);
  updateLayout();

  return Object.freeze({
    setController(value) {
      const next = value || null;
      if (controller !== next) {
        releaseAll();
        controller?.setTouchGamepad?.(null);
        controller = next;
      }
      publish();
    },
    setEnabled(value) {
      const next = !!value;
      if (!next) releaseAll();
      enabled = next;
      root.hidden = !enabled;
      root.setAttribute('aria-hidden', String(!enabled));
      publish();
      if (enabled) updateLayout();
    },
    setOpacity(value) {
      const number = Number(value);
      opacity = clamp(Number.isFinite(number) ? number : 0.55, 0.25, 0.75);
      root.style.setProperty('--touch-opacity', String(opacity));
    },
    clearInputs: releaseAll,
    destroy() {
      releaseAll();
      enabled = false;
      root.hidden = true;
      root.setAttribute('aria-hidden', 'true');
      controller = null;
      resizeObserver?.disconnect();
      window.removeEventListener('resize', updateLayout);
      window.removeEventListener('orientationchange', updateLayout);
      window.visualViewport?.removeEventListener('resize', updateLayout);
      window.visualViewport?.removeEventListener('scroll', updateLayout);
      document.removeEventListener('fullscreenchange', updateLayout);
      window.removeEventListener('blur', onBlur);
      document.removeEventListener('visibilitychange', onVisibility);
      window.removeEventListener('pagehide', onPagehide);
      probe.remove();
      root.replaceChildren();
      root.classList.remove('touch-controls');
      root.removeAttribute('role');
      root.removeAttribute('aria-label');
      root.removeAttribute('aria-hidden');
      root.removeAttribute('style');
    },
  });
}
