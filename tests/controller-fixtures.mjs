export function standardPad(index = 0, id = 'Xbox Wireless Controller (STANDARD GAMEPAD)') {
  return {index, id, mapping: 'standard', connected: true,
    buttons: Array.from({length: 17}, () => ({pressed: false, value: 0})), axes: [0, 0, 0, 0]};
}

// Constructed raw-layout fixture: indices from the SDL Mayflash face/stick
// mapping, plus explicitly authored digital clicks and an encoded HID hat.
// This is a conversion regression, NOT a physical Mayflash acceptance capture.
const b = index => ({kind:'button', index});
const a = (index, rest=0, end=1) => ({kind:'axis',index,rest,end});
export const rawProfile = {version:1,name:'Constructed GameCube conversion fixture',gamecube:true,
  buttons:{A:b(1),B:b(2),X:b(0),Y:b(3),Z:b(7),Start:b(9),L:b(4),R:b(5),
    Up:{kind:'hat',index:6,direction:0},Right:{kind:'hat',index:6,direction:2},
    Down:{kind:'hat',index:6,direction:4},Left:{kind:'hat',index:6,direction:6}},
  axes:{stickX:a(0),stickY:a(1,0,-1),cstickX:a(5),cstickY:a(2,0,-1),triggerL:a(3,-1),triggerR:a(4,-1)}};
export function rawPad(index=0) {
  const pad=standardPad(index,'MAYFLASH GameCube Controller Adapter (Vendor: 0079 Product: 1843)');
  pad.mapping=''; pad.buttons=pad.buttons.slice(0,12); pad.axes=[0,0,0,-1,-1,0,3.2857142857]; return pad;
}

// The attached 0079:1843 HID descriptor + Chromium/macOS usage indexing predict
// this shape. Values below are authored, not recorded physical button presses.
export function mayflashMacPad(index=0) {
  const pad = rawPad(index);
  pad.buttons = Array.from({length:16}, () => ({pressed:false,value:0}));
  pad.axes = [0,0,0,-1,-1,0,0,0,0,3.2857142857];
  return pad;
}
