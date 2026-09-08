import {createMatchFlow, ROSTER, PHASES} from './match-flow.mjs';

// Disposable browser selection scaffold: remove when original HSD CSS/SSS run.
// Match simulation remains in the native owner; this is not menu accuracy.
export function mountMatchMenu({root, gamePanel, launch, unload, onPhase}) {
  const flow=createMatchFlow();
  let enabled=false, returning=false, activePlayer=0, previousButtons=0, lastView='',lastPhase='',needsRelease=true;
  root.innerHTML=`<div class="menu-heading"><span id="menu-step"></span><h2 id="menu-title"></h2><p id="menu-notice" role="status"></p></div>
    <section id="character-screen"><div class="player-slots"><button type="button" id="player-0"></button><button type="button" id="player-1"></button></div>
    <p>All fighters unlocked. Mario is available; more fighters are coming soon.</p><div id="roster" class="roster" aria-label="Fighters"></div>
    <div class="menu-actions"><label>Stocks <input id="stock-count" type="number" min="1" max="99" value="4"></label><button id="choose-stage" class="primary">Choose stage →</button></div></section>
    <section id="stage-screen" hidden><button id="fd-stage" class="stage-card" aria-label="Start match on Final Destination"><span class="stage-art" aria-hidden="true"></span><strong>Final Destination</strong><span>Start match</span></button><div class="menu-actions"><button id="back-characters">← Characters</button><span id="match-summary"></span></div></section>
    <p class="menu-help">Arrows / D-pad: navigate · Enter / A: select · Escape / B: back · Start: continue</p><p id="menu-loading" hidden>Preparing match…</p>`;
  const $=id=>root.querySelector('#'+id);
  const notice=text=>{$('menu-notice').textContent=text;};
  for(const character of ROSTER){
    const card=document.createElement('button');card.type='button';card.dataset.character=character.id;
    card.className='fighter-card'+(character.supported?' available':'');
    const name=document.createElement('strong');name.textContent=character.name;
    const availability=document.createElement('span');availability.textContent=character.supported?'Available':'Not available yet';
    card.append(name,availability);card.disabled=!character.supported;
    card.onclick=()=>act(()=>{flow.selectCharacter(activePlayer,character.id);notice(`Player ${activePlayer+1}: ${character.name}`);});
    $('roster').append(card);
  }
  function render(focus=false){
    const s=flow.getState(), key=[s.revision,enabled,returning,activePlayer].join(':');
    if(key===lastView&&!focus)return;lastView=key;
    if(s.phase!==lastPhase){lastPhase=s.phase;needsRelease=true;}
    root.dataset.phase=s.phase;root.hidden=s.phase===PHASES.PLAYING&&!returning;
    gamePanel.classList.toggle('menu-background',s.phase!==PHASES.PLAYING||returning);
    gamePanel.inert=s.phase!==PHASES.PLAYING||returning;
    $('character-screen').hidden=s.phase!==PHASES.CHARACTER_SELECT;
    $('stage-screen').hidden=s.phase!==PHASES.STAGE_SELECT;
    $('menu-loading').hidden=s.phase!==PHASES.LOADING&&!returning;
    $('menu-loading').textContent=returning?'Returning to character select…':'Preparing match…';
    $('menu-step').textContent=s.phase===PHASES.CHARACTER_SELECT?'01 / FIGHTERS':'02 / STAGE';
    $('menu-title').textContent=s.phase===PHASES.CHARACTER_SELECT?'Choose your fighters':s.phase===PHASES.STAGE_SELECT?'Choose a stage':returning?'Choose your fighters':'Get ready';
    for(let slot=0;slot<2;slot++){
      const name=ROSTER.find(c=>c.id===s.players[slot].characterId).name;
      $('player-'+slot).textContent=`P${slot+1} · ${name}`;
      $('player-'+slot).setAttribute('aria-pressed',String(slot===activePlayer));
    }
    $('stock-count').value=s.stocks;
    $('match-summary').textContent=`${s.stocks} stock${s.stocks===1?'':'s'} · Mario vs Mario`;
    for(const control of root.querySelectorAll('button,input')){
      const character=control.dataset.character&&ROSTER.find(c=>c.id===control.dataset.character);
      control.disabled=!enabled||returning||s.phase===PHASES.LOADING||!!character&&!character.supported;
      if(character)control.setAttribute('aria-pressed',String(s.players[activePlayer].characterId===character.id));
    }
    onPhase?.(s.phase);
    if(focus&&enabled){
      (s.phase===PHASES.CHARACTER_SELECT?$('choose-stage'):$('fd-stage')).focus();
      root.scrollIntoView({block:'start'});
    }
  }
  function act(action){if(!enabled||returning)return;try{action();render();}catch(error){notice(error.message);render();}}
  for(let slot=0;slot<2;slot++)$('player-'+slot).onclick=()=>act(()=>{activePlayer=slot;});
  $('stock-count').onchange=()=>act(()=>flow.setStocks(Number($('stock-count').value)));
  $('choose-stage').onclick=()=>act(()=>{flow.setStocks(Number($('stock-count').value));flow.confirmCharacters();notice('Select Final Destination to begin.');render(true);});
  $('back-characters').onclick=()=>act(()=>{flow.back();notice('');render(true);});
  async function start(options={}){
    if(!enabled||returning||flow.getState().phase===PHASES.LOADING)return false;
    if(options.diagnostic){
      if(flow.getState().phase===PHASES.PLAYING)flow.returnToCharacters();
      if(flow.getState().phase===PHASES.CHARACTER_SELECT)flow.confirmCharacters();
    }
    if(flow.getState().phase!==PHASES.STAGE_SELECT)return false;
    flow.beginLaunch();notice('');render();
    try{
      if(!await launch(flow.getState().launch,options))throw Error('Match could not start. Check the runtime status below.');
      flow.launchSucceeded();render();return true;
    }catch(error){flow.launchFailed();notice(error.message);render(true);return false;}
  }
  $('fd-stage').onclick=()=>{if(!enabled||returning||flow.getState().phase!==PHASES.STAGE_SELECT)return;flow.selectStage('final-destination');return start();};
  async function finish(result){
    if(returning||flow.getState().phase!==PHASES.PLAYING)return;
    returning=true;render();
    try{
      if(!await unload())throw Error('Could not close the match. Check runtime status.');
      flow.returnToCharacters();notice(result?.winner>=0?`Player ${result.winner+1} wins. Ready for another match?`:'Choose your fighters.');
    }catch(error){notice(error.message);}
    finally{returning=false;render(true);}
  }
  function navigate(direction){
    const controls=[...root.querySelectorAll('button,input')].filter(e=>!e.disabled&&e.getClientRects().length);
    const index=controls.indexOf(document.activeElement);
    controls[(index+direction+controls.length)%controls.length]?.focus();
  }
  root.addEventListener('keydown',event=>{
    if(event.repeat){if(event.key==='Enter'||event.key===' ')event.preventDefault();return;}
    if(!enabled||returning)return;
    if(event.target.matches('input'))return;
    if(['ArrowRight','ArrowDown','ArrowLeft','ArrowUp'].includes(event.key)){
      event.preventDefault();navigate(['ArrowLeft','ArrowUp'].includes(event.key)?-1:1);
    }else if(event.key==='Escape'&&flow.getState().phase===PHASES.STAGE_SELECT){event.preventDefault();$('back-characters').click();}
  });
  render();
  return {
    setEnabled(value){const becameEnabled=!enabled&&!!value;enabled=!!value;render(becameEnabled&&[PHASES.CHARACTER_SELECT,PHASES.STAGE_SELECT].includes(flow.getState().phase));},
    reset(){if(flow.getState().phase===PHASES.PLAYING)flow.returnToCharacters();else if(flow.getState().phase===PHASES.STAGE_SELECT)flow.back();notice('');render(true);},
    observeMatch(data){if(data.finished&&flow.getState().phase===PHASES.PLAYING&&!returning){document.getElementById('last-match').textContent=JSON.stringify(data);void finish(data);}},
    startDiagnostic(kind){return start({diagnostic:kind});},
    returnToCharacters(){return finish();},
    pollButtons(buttons){
      if(needsRelease){previousButtons=buttons;if(!buttons)needsRelease=false;return;}
      const edges=buttons&~previousButtons;previousButtons=buttons;
      if(!enabled||returning||root.hidden||document.hidden||!document.hasFocus())return;
      if(edges&0x200){if(flow.getState().phase===PHASES.STAGE_SELECT)$('back-characters').click();return;}
      if(edges&0x1000){(flow.getState().phase===PHASES.CHARACTER_SELECT?$('choose-stage'):$('fd-stage')).click();return;}
      if(edges&0x100){const focused=document.activeElement;if(root.contains(focused)&&focused.matches('button:not(:disabled)'))focused.click();else (flow.getState().phase===PHASES.CHARACTER_SELECT?$('choose-stage'):$('fd-stage')).focus();return;}
      if(edges&0x5)navigate(-1);else if(edges&0xA)navigate(1);
    },
  };
}
