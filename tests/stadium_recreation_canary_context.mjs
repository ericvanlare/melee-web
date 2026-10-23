// One canary's immutable report/approval ownership contract. Browser wiring
// must separately prove returned Asyncify idle before dispatching native calls.
export function createStadiumRecreationCanaryContext({reportA, checkpointA, fullPair = false}) {
  if (typeof fullPair !== 'boolean') throw Error('Canary full-pair mode must be boolean');
  if (!reportA || !checkpointA) throw Error('Canary requires lifetime A report and checkpoint');
  let phase = 'A', archivedA = null, archivedNativeA = null, admittedGeneration = null;
  let closeReceipt = null, closeBytes = null;
  const sameA = () => JSON.stringify(reportA) === archivedA;
  const completeA = () => {
    const cf = reportA.scenario?.matched_setup_cf;
    const generation = reportA.scenario?.first_sss_vs_setup?.observation?.first_sss_vs_setup?.match_world_generation;
    const count = fullPair ? 7167 : 174;
    return cf?.actions_completed === count && cf.actions?.length === count &&
      cf.decision === (fullPair ? 'PASS_ORIGINAL_FULL7167_3501_TICK_15_COMMON_FIELDS_AND_SEED_C_F_GO'
        : 'PASS_RUN2_PROPER_PREFIX_C_F_STATE_ONLY') &&
      (!fullPair || (cf.common_field_comparisons_passed === 3501 &&
        cf.seed_value_comparisons_passed === 3501 && cf.exact_value_comparisons_passed === 56016 &&
        cf.last_native_state?.action_count === 7167 &&
        cf.last_native_state?.schema === 'melee-web-stadium-source-action-native-v5' &&
        cf.last_native_state?.source_ticks === 3585 && cf.last_native_state?.source_draws === 3582)) &&
      cf.last_native_state?.complete === true && cf.last_native_state?.failed === false &&
      cf.last_native_state?.cursor === count && Number.isSafeInteger(generation) && generation > 0;
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
