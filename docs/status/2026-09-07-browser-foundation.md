# Browser foundation

The inspector renders original HSD transforms, skinning, materials, texture
layers and polygons through Aurora GX/WebGPU. The runtime now also renders the
authored Final Destination stage, Mario effects and ground/air fireballs through
the integrated stage and item services. Mario/Fox Wait and walking clips and the
bucket/fan/bullseye fixtures were visually checked in earlier milestones. The
static inspector still uses its own inspection cameras and lights.

Browser keyboard input reaches Aurora PAD with distinct raw/clamped diagnostics
and focus neutralization. Physical controllers remain untested. The runtime
fighter now uses this browser input/presentation path; visual correctness remains open.
