/** Opt-in, host-side perturbations for the 2026-10-06 gameplay pause trace
 * (experiment 2). Local diagnostic helper; none of this touches the runtime,
 * source ticks, thresholds, audio or GPU work of the page under test. Each
 * perturbation is an external host condition (a busy main-thread interval, a
 * second headless page that loads the shared GPU, or CPU spinner processes)
 * that is logged with page-clock timestamps so the per-callback timing table
 * can be compared inside and outside the perturbed windows. */
import {spawn} from 'node:child_process';

/** Source-frame schedule. Frames are the polled Mario P0 frame counter. */
export const PERTURB_SCHEDULE=Object.freeze([
  {frame:600, kind:'stall', ms:25,  phase:'stall-ladder-idle-host'},
  {frame:780, kind:'stall', ms:50,  phase:'stall-ladder-idle-host'},
  {frame:960, kind:'stall', ms:75,  phase:'stall-ladder-idle-host'},
  {frame:1140,kind:'stall', ms:100, phase:'stall-ladder-idle-host'},
  {frame:1500,kind:'gpu',   on:true, iterations:300, phase:'gpu-load-300'},
  {frame:1900,kind:'gpu',   on:false,phase:'gpu-load-300'},
  {frame:2100,kind:'gpu',   on:true, iterations:600, phase:'gpu-load-600'},
  {frame:2500,kind:'gpu',   on:false,phase:'gpu-load-600'},
  {frame:2800,kind:'cpu',   on:true, phase:'cpu-spinners-only'},
  {frame:3400,kind:'cpu',   on:false,phase:'cpu-spinners-only'},
  {frame:3700,kind:'gpu',   on:true, iterations:600, phase:'gpu-load-600-plus-stalls'},
  {frame:3880,kind:'stall', ms:50,  phase:'gpu-load-600-plus-stalls'},
  {frame:4060,kind:'stall', ms:100, phase:'gpu-load-600-plus-stalls'},
  {frame:4240,kind:'stall', ms:100, phase:'gpu-load-600-plus-stalls'},
  {frame:4420,kind:'gpu',   on:false,phase:'gpu-load-600-plus-stalls'},
  {frame:4650,kind:'end',   phase:'schedule-complete'},
]);

export const HEAVY_GPU_DEFAULTS=Object.freeze({size:2048,iterations:300,passes:1});

export function heavyGpuHtml({size,iterations,passes}){
  return `<!doctype html><meta charset=utf-8><title>gpu load</title><canvas id=c width=${size} height=${size}></canvas><script>
const gl=document.getElementById('c').getContext('webgl2',{antialias:false,powerPreference:'high-performance',preserveDrawingBuffer:false});
const stats={on:false,iterations:${iterations},passes:${passes},size:${size},batches:0,durations:[],max_ms:0,context_ok:!!gl,
  renderer:null,started_at_ms:null,stopped_at_ms:null,errors:[]};
window.__gpuLoad=stats;
if(gl){
  const info=gl.getExtension('WEBGL_debug_renderer_info');
  stats.renderer=info?gl.getParameter(info.UNMASKED_RENDERER_WEBGL):null;
  const compile=(type,source)=>{const shader=gl.createShader(type);gl.shaderSource(shader,source);gl.compileShader(shader);
    if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))stats.errors.push(gl.getShaderInfoLog(shader));return shader;};
  const program=gl.createProgram();
  gl.attachShader(program,compile(gl.VERTEX_SHADER,'#version 300 es\\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}'));
  gl.attachShader(program,compile(gl.FRAGMENT_SHADER,'#version 300 es\\nprecision highp float;uniform float seed;uniform int n;out vec4 o;void main(){float a=gl_FragCoord.x*0.001+seed,b=gl_FragCoord.y*0.001;for(int i=0;i<4096;i++){if(i>=n)break;a=sin(a*1.0001+b)+cos(b*0.9999-a);b=fract(a*b+0.37);}o=vec4(a,b,a*b,1.0);}'));
  gl.linkProgram(program);
  if(!gl.getProgramParameter(program,gl.LINK_STATUS))stats.errors.push(gl.getProgramInfoLog(program));
  gl.useProgram(program);
  const seed=gl.getUniformLocation(program,'seed');
  const pixel=new Uint8Array(4);
  const bound=gl.getUniformLocation(program,'n');
  gl.viewport(0,0,${size},${size});
  const batch=()=>{
    if(stats.on){
      const start=performance.now();
      gl.uniform1i(bound,stats.iterations);gl.uniform1f(seed,stats.batches);gl.drawArrays(gl.TRIANGLES,0,3);
      gl.readPixels(0,0,1,1,gl.RGBA,gl.UNSIGNED_BYTE,pixel); // synchronous readback waits for the GPU (gl.finish does not)
      const duration=performance.now()-start;
      stats.batches++;if(stats.durations.length<4096)stats.durations.push(duration);
      if(duration>stats.max_ms)stats.max_ms=duration;
    }
    setTimeout(batch,0);
  };
  batch();
}
</script>`;
}

/** Create the idle GPU-load page before the experiment so context creation is
 * not part of any measured window. */
export async function createHeavyGpuPage(browser,options={}){
  const config={...HEAVY_GPU_DEFAULTS,...options};
  const page=await browser.newPage({viewport:{width:640,height:480},deviceScaleFactor:1});
  await page.setContent(heavyGpuHtml(config));
  const ready=await page.evaluate(()=>({context_ok:window.__gpuLoad.context_ok,
    renderer:window.__gpuLoad.renderer,errors:window.__gpuLoad.errors.slice()}));
  if(!ready.context_ok||ready.errors.length)
    throw Error('GPU-load page did not initialize: '+JSON.stringify(ready));
  return {page,config,ready};
}

export async function setHeavyGpu(heavy,on,iterations=heavy.config.iterations){
  return heavy.page.evaluate(([value,count])=>{const stats=window.__gpuLoad;
    if(value){stats.durations.length=0;stats.max_ms=0;stats.iterations=count;stats.started_at_ms=performance.now();}
    else stats.stopped_at_ms=performance.now();
    stats.on=value;return {on:stats.on,iterations:stats.iterations,batches:stats.batches,at_ms:performance.now()};},[on,iterations]);
}

export async function readHeavyGpu(heavy){
  return heavy.page.evaluate(()=>{const stats=window.__gpuLoad;
    const sorted=stats.durations.slice().sort((a,b)=>a-b);
    const pick=fraction=>sorted.length?sorted[Math.min(sorted.length-1,Math.floor(fraction*sorted.length))]:null;
    return {batches:stats.batches,window_batches:sorted.length,p50_ms:pick(0.5),p95_ms:pick(0.95),max_ms:stats.max_ms,
      errors:stats.errors.slice(),on:stats.on};});
}

/** Self-terminating CPU spinners (python3 busy loops, ordinary priority). The
 * marker in argv lets ps verify that none outlive the run. */
export const SPINNER_MARKER='claude-pause-trace-spin';
export function startCpuSpinners(count,maxSeconds){
  const code=`import sys,time\nend=time.time()+${Number(maxSeconds)}\nwhile time.time()<end:\n    pass\n`;
  return Array.from({length:count},()=>spawn('python3',['-c',code,SPINNER_MARKER],{stdio:'ignore'}));
}
export async function stopCpuSpinners(spinners){
  for(const child of spinners)try{child.kill('SIGTERM');}catch(_){/* already gone */}
  await Promise.all(spinners.map(child=>child.exitCode!==null||child.signalCode!==null?null:
    new Promise(resolve=>{child.once('exit',resolve);setTimeout(resolve,2000);})));
}

/** Busy main-thread interval inside the page under test, between its tasks. */
export async function injectStall(page,ms){
  return page.evaluate(duration=>{
    const start=performance.now();const end=start+duration;
    while(performance.now()<end){/* observation-host stall; no page state is touched */}
    return {start_ms:start,end_ms:performance.now()};
  },ms);
}
