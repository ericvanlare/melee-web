// Versioned visible-play inventories. These inputs enter through the same raw
// PAD boundary as a controller and are consumed by the original source tick.
// Add a fighter-specific inventory here before admitting a new fighter.
export const PAD={Z:0x10,L:0x40,R:0x20,A:0x100,B:0x200,X:0x400,Y:0x800};
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

export const actionInventories=new Map([
 [18,{id:'marth-visible-actions-v1',fighter:'Marth',minimumStageFrames:4200,cases:[...common,...wavedashes,...marth]}],
]);

export function actionInventory(fighterKind){return actionInventories.get(fighterKind)||null;}
