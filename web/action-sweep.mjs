// Versioned visible-play inventories. These inputs enter through the same raw
// PAD boundary as a controller and are consumed by the original source tick.
// Add a fighter-specific inventory here before admitting a new fighter.
export const PAD={DLEFT:0x1,DRIGHT:0x2,DUP:0x8,DDOWN:0x4,Z:0x10,L:0x40,R:0x20,A:0x100,B:0x200,X:0x400,Y:0x800};
const input=(duration=1,buttons=0,stickX=0,stickY=0,cstickX=0,cstickY=0,triggerL=0,triggerR=0)=>
 ({duration,buttons,stickX,stickY,cstickX,cstickY,triggerL,triggerR});
const ground=(name,expect,...inputs)=>({name,expect,settle:true,inputs});
const aerial=(name,expect,cstickX=0,cstickY=0,buttons=0)=>({name,expect,settle:true,inputs:[
 input(2,PAD.X),input(4),input(2,buttons,0,0,cstickX,cstickY),input(150)
]});

const common=[
 ground('walk left/right',[[15,17]],input(12,0,-30),input(8),input(12,0,30),input(30)),
 ground('dash left/right',[[20,20]],input(8,0,-80),input(20),input(8,0,80),input(45)),
 ground('crouch',[[39,41]],input(15,0,0,-80),input(30)),
 ground('jab',[[44,44]],input(1,PAD.A),input(55)),
 ground('forward tilt',[[51,55]],input(2,0,40),input(1,PAD.A,40),input(70)),
 ground('up tilt',[[56,56]],input(2,0,0,40),input(1,PAD.A,0,40),input(70)),
 ground('down tilt',[[57,57]],input(2,0,0,-40),input(1,PAD.A,0,-40),input(70)),
 ground('forward smash',[[58,62]],input(1,0,0,0,80),input(90)),
 ground('up smash',[[63,63]],input(1,0,0,0,0,80),input(90)),
 ground('down smash',[[64,64]],input(1,0,0,0,0,-80),input(90)),
 ground('dash attack',[[50,50]],input(7,0,-80),input(1,PAD.A,-80),input(80)),
 ground('shield',[[178,180]],input(18,PAD.L,0,0,0,0,255),input(35)),
 ground('forward roll',[[233,234]],input(4,PAD.L,0,0,0,0,255),input(1,PAD.L,80,0,0,0,255),input(70)),
 ground('back roll',[[233,234]],input(4,PAD.L,0,0,0,0,255),input(1,PAD.L,-80,0,0,0,255),input(70)),
 ground('spot dodge',[[235,235]],input(4,PAD.L,0,0,0,0,255),input(1,PAD.L,0,-80,0,0,255),input(70)),
 ground('grab',[[212,218]],input(1,PAD.Z),input(80)),
 aerial('neutral aerial',[[65,65]],0,0,PAD.A),
 aerial('forward aerial',[[66,66]],80,0),
 aerial('back aerial',[[67,67]],-80,0),
 aerial('up aerial',[[68,68]],0,80),
 aerial('down aerial',[[69,69]],0,-80),
 {name:'air dodge',expect:[[236,236]],settle:true,inputs:[input(2,PAD.X),input(5),input(1,PAD.L,0,0,0,0,255),input(150)]},
];

const wavedashes=[];
for(const direction of [-1,1])for(let n=1;n<=10;n++)wavedashes.push({
 name:`wavedash ${direction<0?'left':'right'} ${n}/10`,expect:[[43,43]],settle:true,
 inputs:[input(1,PAD.X),input(3),input(1,PAD.L,40*direction,-40,0,0,255),input(70)]
});

const marth=[
 ground('Shield Breaker charge/release',[[341,348]],input(45,PAD.B),input(120)),
 {name:'Dancing Blade chain',expect:[[349,349],[350,366]],settle:true,inputs:Array.from({length:8},()=>[input(1,PAD.B,80),input(6)]).flat().concat(input(140))},
 ground('Dolphin Slash',[[367,368]],input(1,PAD.B,0,80),input(360)),
 ground('Counter',[[369,372]],input(1,PAD.B,0,-80),input(120)),
];

// The common table is shared by every source fighter, but the self-motion
// ranges below are deliberately kept separate.  Dr. Mario inherits Mario's
// implementation while Roy inherits Marth's implementation; their source
// action tables still have different branch identities and must be exercised
// by their own inventories.
const aerialSpecial=(name,expect,stickX=0,stickY=0)=>({
  name,expect,settle:true,inputs:[
    input(2,PAD.X),input(4),input(1,PAD.B,stickX,stickY),input(180),
  ],
});

const drMario=[
  {name:'Dr. Mario taunt',expect:[[341,342]],settle:true,inputs:[input(1,PAD.DUP),input(90)]},
  ground('Megavitamin',[[343,343]],input(1,PAD.B),input(150)),
  aerialSpecial('Megavitamin (air)',[[344,344]]),
  ground('Super Sheet',[[345,345]],input(1,PAD.B,80),input(150)),
  aerialSpecial('Super Sheet (air)',[[346,346]],80),
  ground('Super Jump Punch',[[347,347]],input(1,PAD.B,0,80),input(180)),
  aerialSpecial('Super Jump Punch (air)',[[348,348]],0,80),
  ground('Dr. Tornado',[[349,349]],input(1,PAD.B,0,-80),input(150)),
  aerialSpecial('Dr. Tornado (air)',[[350,350]],0,-80),
];

const roy=[
  // This bounded charge checks start, loop and the normal release, not the
  // maximum-charge exit or the airborne variants.
  ground('Flare Blade charge/release',[[341,341],[342,342],[343,343]],input(45,PAD.B),input(120)),
  // Representative grounded chain: the source opening state 349 is followed
  // by real B presses using the same timing as the existing Marth sweep.
  {name:'Double-Edge Dance chain (representative)',expect:[[349,349],[350,357]],settle:true,
    inputs:Array.from({length:8},()=>[input(1,PAD.B,80),input(6)]).flat().concat(input(140))},
  ground('Blazer',[[367,367]],input(1,PAD.B,0,80),input(360)),
  aerialSpecial('Blazer (air)',[[368,368]],0,80),
  ground('Counter stance',[[369,369]],input(1,PAD.B,0,-80),input(150)),
  aerialSpecial('Counter stance (air)',[[371,371]],0,-80),
  // Pending target/air recipes: Flare Blade air 345-348, Double-Edge Dance
  // air 358-366, and Counter hit exits 370/372 need additional source state.
];

const link=[
  ground('Bow charge/release',[[344,346]],input(45,PAD.B),input(150)),
  aerialSpecial('Bow (air)',[[347,349]]),
  ground('Boomerang',[[350,351]],input(1,PAD.B,80),input(180)),
  aerialSpecial('Boomerang (air)',[[353,354]],80),
  ground('Spin Attack',[[356,356]],input(1,PAD.B,0,80),input(240)),
  aerialSpecial('Spin Attack (air)',[[357,357]],0,80),
  ground('Bomb',[[358,358]],input(1,PAD.B,0,-80),input(180)),
  aerialSpecial('Bomb (air)',[[359,359]],0,-80),
];

export const actionInventories=new Map([
 [18,{id:'marth-visible-actions-v1',fighter:'Marth',minimumStageFrames:4200,cases:[...common,...wavedashes,...marth]}],
 [21,{id:'dr-mario-visible-actions-v1',fighter:'Dr. Mario',minimumStageFrames:4800,cases:[...common,...wavedashes,...drMario]}],
 [26,{id:'roy-visible-actions-v1',fighter:'Roy',minimumStageFrames:4800,cases:[...common,...wavedashes,...roy]}],
 [6,{id:'link-visible-actions-v1',fighter:'Link',minimumStageFrames:4800,cases:[...common,...wavedashes,...link]}],
 [20,{id:'young-link-visible-actions-v1',fighter:'Young Link',minimumStageFrames:4800,cases:[...common,...wavedashes,...link]}],
]);

export function actionInventory(fighterKind){return actionInventories.get(fighterKind)||null;}
