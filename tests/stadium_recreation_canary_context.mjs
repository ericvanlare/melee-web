// One canary's immutable report/approval ownership contract. Browser wiring
// must separately prove returned Asyncify idle before dispatching native calls.
export function createStadiumRecreationCanaryContext({reportA, checkpointA}) {
  if (!reportA || !checkpointA) throw Error('Canary requires lifetime A report and checkpoint');
  let phase = 'A', archivedA = null, archivedNativeA = null, admittedGeneration = null;
  let closeReceipt = null, closeBytes = null;
  const sameA = () => JSON.stringify(reportA) === archivedA;
  const completeA = () => {
    const cf = reportA.scenario?.matched_setup_cf;
    const generation = reportA.scenario?.first_sss_vs_setup?.observation?.first_sss_vs_setup?.match_world_generation;
    return cf?.actions_completed === 174 && cf.actions?.length === 174 &&
      cf.decision === 'PASS_RUN2_PROPER_PREFIX_C_F_STATE_ONLY' &&
      cf.last_native_state?.complete === true && cf.last_native_state?.failed === false &&
      cf.last_native_state?.cursor === 174 && Number.isSafeInteger(generation) && generation > 0;
  };
  return Object.freeze({
    async archiveA(persist) {
      if (phase !== 'A' || !completeA() || typeof persist !== 'function')
        throw Error('Canary requires compared complete lifetime A');
      // Snapshot before persistence and exclude every reentrant operation while
      // it owns the original report/checkpoint. Any mutation refuses permission.
      const bytes = JSON.stringify(reportA);
      const snapshot = JSON.parse(bytes);
      phase = 'persisting-A';
      try {
        await persist(reportA, checkpointA);
        if (JSON.stringify(reportA) !== bytes) throw Error('Canary lifetime A changed during persistence');
        archivedA = bytes;
        archivedNativeA = snapshot.scenario.matched_setup_cf.last_native_state;
        admittedGeneration = snapshot.scenario.first_sss_vs_setup.observation.first_sss_vs_setup.match_world_generation;
        phase = 'A-persisted';
      } catch (error) { phase = 'failed'; throw error; }
    },
    beforeClose() { return phase === 'A-persisted' && sameA(); },
    async archiveClose(receipt, persist) {
      if (phase !== 'A-persisted' || !sameA() || receipt?.schema !== 'melee-web-stadium-recreation-boundary-v1' ||
          receipt.attempted !== true || receipt.closed !== true || receipt.restarted !== false || receipt.error !== null ||
          receipt.match_to_host_restored !== true || receipt.host_to_parent_restored !== true ||
          receipt.prior_world_generation !== admittedGeneration ||
          JSON.stringify(receipt.matched_setup_cf) !== JSON.stringify(archivedNativeA) ||
          typeof persist !== 'function') throw Error('Canary requires exact archived A native closure');
      const bytes = JSON.stringify(receipt);
      const saved = JSON.parse(bytes);
      phase = 'persisting-close';
      try {
        await persist(saved);
        if (!sameA() || JSON.stringify(saved) !== bytes || JSON.stringify(receipt) !== bytes)
          throw Error('Canary closure changed during persistence');
        closeReceipt = JSON.parse(bytes);closeBytes = bytes;phase = 'A-closed-persisted';
      } catch (error) { phase = 'failed'; throw error; }
    },
    afterClose() { return phase === 'A-closed-persisted' && sameA(); },
    acceptRestart(receipt) {
      // All fields, including every seed, attempted and the complete archived
      // native receipt, must match; restarted is the sole permitted change.
      const expected = closeReceipt && {...closeReceipt, restarted: true};
      if (phase !== 'A-closed-persisted' || !sameA() ||
          JSON.stringify(closeReceipt) !== closeBytes || JSON.stringify(receipt) !== JSON.stringify(expected))
        throw Error('Canary requires the exact returned acknowledged restart receipt');
      phase = 'restarted';
    },
    beginB(nextReport, nextCheckpoint) {
      if (phase !== 'restarted' || !sameA() || !nextReport || !nextCheckpoint ||
          nextReport === reportA || nextCheckpoint === checkpointA ||
          nextReport.scenario === reportA.scenario || nextReport.scenario?.matched_setup_cf)
        throw Error('Canary lifetime B requires distinct fresh report/checkpoint owners');
      phase = 'B';
      return {report: nextReport, checkpoint: nextCheckpoint, priorWorldGeneration: admittedGeneration};
    },
    status() { return {phase, archivedA, closeReceipt: closeReceipt && structuredClone(closeReceipt)}; },
  });
}
