import assert from 'node:assert/strict';
export function readSparseControllerReady(){
 const m=Module.meleeControllers,rows=m.inspect();
 return [0,1,2,3].map(p=>m.getPortSource(p)).join(',')==='keyboard,off,controller,off'&&
  rows.length===1&&rows[0].port===2&&rows[0].active&&rows[0].output.buttons===0&&
  rows[0].output.stick.every(x=>x===0)&&rows[0].output.cstick.every(x=>x===0)&&
  rows[0].output.triggers.every(x=>x===0);
}
export const SPARSE_DEVICE_LABEL='Player port for Sparse Mario authored standard Gamepad';
// Shared actual UI flow; never assign the controller manager directly.
export async function configureSparseControllerSettings({page,retention={},deviceLabel=SPARSE_DEVICE_LABEL,selectorTimeout=10000}){
 let primary;
 await page.locator('#controls-open').click();
 try{
  await page.locator('#keyboard-layout').selectOption('two');
  for(const [name,mode] of [['one','keyboard'],['two','off'],['three','controller'],['four','off']])
   await page.locator(`#player-${name}-source`).selectOption(mode);
  await page.locator('#controller-advanced > summary').click();
  await page.locator('[data-controller-panel]').waitFor({timeout:selectorTimeout});
  assert(await page.locator('#controller-advanced').evaluate(node=>node.open));
  await page.getByLabel(deviceLabel,{exact:true}).selectOption('2',{timeout:selectorTimeout});
  retention.advancedRows=await page.evaluate(()=>Module.meleeControllers.inspect());
  assert.equal(retention.advancedRows.length,1);
  assert.equal(retention.advancedRows[0].port,2);
  assert.equal(retention.advancedRows[0].active,false,'Advanced testing must suspend device input');
 }catch(error){primary=error;retention.primaryError=String(error);}
 finally{
  try{
   if(await page.locator('#controls-dialog').evaluate(node=>node.open))await page.locator('#controls-close').click();
   assert.equal(await page.locator('[data-controller-panel]').count(),0);
   assert.equal(await page.locator('#controls-dialog').evaluate(node=>node.open),false);
   retention.dialogClosed=true;
  }catch(error){retention.closeError=String(error);if(!primary)primary=error;}
 }
 if(primary)throw primary;
 await page.waitForFunction(readSparseControllerReady,undefined,{timeout:10000});
 retention.activeRows=await page.evaluate(()=>Module.meleeControllers.inspect());
 return retention.activeRows;
}
