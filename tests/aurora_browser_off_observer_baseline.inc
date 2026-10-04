// Reviewed ordinary browser observer baseline.
// This is compared against the OFF projection extracted from the pinned Aurora base plus patch.
    if (callback) {
      // The caller's callback finishes and submits the command buffer before
      // this completion observer is registered. Guard the slot pool against
      // a later Aurora shutdown/reinitialization resetting its ownership.
      const double observerStarted = phase_now_ms();
      const auto generation = g_browserStagingGeneration.load(std::memory_order_acquire);
      g_queue.OnSubmittedWorkDone(
          wgpu::CallbackMode::AllowSpontaneous,
          [stagingSlot, generation](wgpu::QueueWorkDoneStatus status, wgpu::StringView message) {
            if (generation != g_browserStagingGeneration.load(std::memory_order_acquire)) {
              return;
            }
            if (status != wgpu::QueueWorkDoneStatus::Success) {
              Log.fatal("Browser staging submission failed: {}", message);
            }
            g_stagingSlots.release(stagingSlot);
          });
      stats.lastEndFrameObserverMs = phase_now_ms() - observerStarted;
    } else {
      g_stagingSlots.release(stagingSlot);
    }
