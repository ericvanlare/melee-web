// Private, one-shot observation of one ordinary match callback. No restore API.
#pragma once
namespace melee_web_menu_browser {
extern unsigned audio_phase;
extern bool transition_audio_continues;
bool frame_effect_terminal();
void frame_effect_callback_started();
void frame_effect_callback_aborted(const char* reason);
void frame_effect_begin_tick(const PADStatus pads[4], unsigned prior_steps,
                             unsigned planned_steps, unsigned planned_audio_steps,
                             bool audio_before_construction, bool clock_stalled);
void frame_effect_tick_returned();
void frame_effect_begin_draw();
void frame_effect_draw_returned();
void frame_effect_audio_returned(unsigned pcm_frames);
void frame_effect_callback_finished(unsigned steps, unsigned draws);
}
