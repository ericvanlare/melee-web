/** Optional diagnostics preferences are separate from Personal progress. */
export const DIAGNOSTICS_PREFERENCE_KEY = 'melee-web-automatic-diagnostics-v1';

export function readDiagnosticsPreference(root = globalThis) {
  try { return root.localStorage?.getItem(DIAGNOSTICS_PREFERENCE_KEY) !== 'off'; }
  catch { return true; }
}

export function writeDiagnosticsPreference(enabled, root = globalThis) {
  try { root.localStorage?.setItem(DIAGNOSTICS_PREFERENCE_KEY, enabled ? 'on' : 'off'); }
  catch { /* The current visit still honors the preference. */ }
}

export function mountDiagnosticsSettings({document = globalThis.document, root = globalThis} = {}) {
  const toggle = document.getElementById('automatic-diagnostics');
  const description = document.getElementById('diagnostics-description');
  const exportButton = document.getElementById('export-diagnostics');
  const status = document.getElementById('diagnostics-export-status');
  let player = null, exporting = false, state = null;
  function render() {
    let preference = null;
    try { preference = player?.getDiagnosticsSettings(); } catch {}
    toggle.disabled = !preference?.eligible;
    toggle.checked = preference?.eligible === true && preference.automatic === true;
    description.textContent = preference?.eligible ?
      'Send bounded technical reports after unexpected pauses. Local diagnostics remain available when reporting is off.' :
      'Diagnostics stay on this device at this address. Automatic reporting is available on WebMelee staging and production.';
    exportButton.disabled = !player || exporting || state?.running === true || state?.busy === true;
    exportButton.title = state?.running ? 'Pause gameplay to export diagnostics.' : '';
  }
  toggle.onchange = () => {
    try { player?.setAutomaticDiagnostics(toggle.checked); } catch {}
    render();
  };
  exportButton.onclick = async () => {
    if (exportButton.disabled) return;
    exporting = true; render();
    let url = null;
    try {
      const report = await player.exportDiagnostics();
      if (!report) throw Error('unavailable');
      const data = new root.Blob([JSON.stringify(report)], {type: 'application/json'});
      url = root.URL.createObjectURL(data);
      const link = document.createElement('a');
      link.href = url; link.download = 'webmelee-diagnostics.json';
      link.click();
      status.textContent = 'Diagnostics exported.';
    } catch { status.textContent = 'Diagnostics are unavailable for this visit.'; }
    finally {
      if (url) root.setTimeout(() => root.URL.revokeObjectURL(url), 1000);
      exporting = false; render();
    }
  };
  render();
  return Object.freeze({
    bindPlayer(value) { player = value; state = player?.getState(); render(); },
    setState(value) { state = value; render(); },
  });
}
