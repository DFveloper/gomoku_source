const $ = selector => document.querySelector(selector);
const board = $('#board'), ctx = board.getContext('2d');
const blackEngine = $('#blackEngine'), whiteEngine = $('#whiteEngine');
const blackLevel = $('#blackLevel'), whiteLevel = $('#whiteLevel');
const randomPlies = $('#randomPlies'), pliesValue = $('#pliesValue');
const rangeRow = $('#rangeRow'), undoOpening = $('#undoOpening');
const startButton = $('#start'), resetButton = $('#reset');
const toastEl = $('#toast');
let state, fetching = false, toastTimer;
let postQueue = Promise.resolve();
let configDraft;

function toast(message) { toastEl.textContent = message; toastEl.hidden = false; clearTimeout(toastTimer); toastTimer = setTimeout(() => toastEl.hidden = true, 2000); }
async function request(path, options = {}) {
  const response = await fetch(path, options), result = await response.json();
  if (!response.ok) throw new Error(result.error || 'Request failed');
  return result;
}
async function post(path, body = {}) {
  postQueue = postQueue.then(async () => {
    try {
      const next = await request(path, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)});
      if (!state || next.revision >= state.revision) { state = next; render(); }
    } catch (error) { toast(error.message); }
  });
  return postQueue;
}

function fillSelects() {
  if (!state || blackEngine.options.length) return;
  for (const engine of state.engines) {
    for (const select of [blackEngine, whiteEngine]) {
      const option = document.createElement('option'); option.value = engine.id;
      option.textContent = engine.name + (engine.available ? '' : ' · runtime unavailable');
      option.disabled = !engine.available; select.append(option);
    }
  }
  for (let level = 1; level <= 5; level++) for (const select of [blackLevel, whiteLevel]) {
    const option = document.createElement('option'); option.value = level; option.textContent = `Level ${level}`; select.append(option);
  }
}

function resizeCanvas() {
  const rect = board.getBoundingClientRect(), ratio = Math.min(devicePixelRatio || 1, 2);
  const width = Math.round(rect.width * ratio), height = Math.round(rect.height * ratio);
  if (board.width !== width || board.height !== height) { board.width = width; board.height = height; }
  ctx.setTransform(ratio,0,0,ratio,0,0); return rect;
}
function circle(x,y,r,fill,stroke,width=1) { ctx.beginPath();ctx.arc(x,y,r,0,Math.PI*2);ctx.fillStyle=fill;ctx.fill();if(stroke){ctx.strokeStyle=stroke;ctx.lineWidth=width;ctx.stroke();} }
function metrics() { const rect=board.getBoundingClientRect(), size=Math.min(rect.width,rect.height), margin=Math.max(18,size*.055);return {rect,size,margin,cell:(size-margin*2)/(state.size-1)}; }
function drawBoard() {
  if (!state) return;
  const rect=resizeCanvas(), {size,margin,cell}=metrics(); ctx.clearRect(0,0,rect.width,rect.height);
  const wood=ctx.createLinearGradient(0,0,size,size);wood.addColorStop(0,'#f1c681');wood.addColorStop(.55,'#ebbb72');wood.addColorStop(1,'#dca35b');ctx.fillStyle=wood;ctx.fillRect(0,0,size,size);
  ctx.strokeStyle='#563e28';ctx.lineWidth=1;
  for(let i=0;i<state.size;i++){const p=margin+i*cell;ctx.beginPath();ctx.moveTo(margin,p);ctx.lineTo(size-margin,p);ctx.stroke();ctx.beginPath();ctx.moveTo(p,margin);ctx.lineTo(p,size-margin);ctx.stroke();}
  for(const r of [3,7,11])for(const c of [3,7,11])circle(margin+c*cell,margin+r*cell,Math.max(3,size*.0047),'#4d3b25');
  for(let r=0;r<state.size;r++)for(let c=0;c<state.size;c++){const stone=state.board[r][c];if(!stone)continue;const x=margin+c*cell,y=margin+r*cell,rad=Math.max(7,cell/2-3),shade=ctx.createRadialGradient(x-rad*.35,y-rad*.45,1,x,y,rad);if(stone===1){shade.addColorStop(0,'#484b48');shade.addColorStop(.45,'#202220');shade.addColorStop(1,'#090a09');}else{shade.addColorStop(0,'#fffef8');shade.addColorStop(.65,'#f1ede0');shade.addColorStop(1,'#c4beaf');}circle(x,y,rad,shade,stone===1?'#050505':'#9e988a');}
  if(state.last){const [r,c]=state.last;circle(margin+c*cell,margin+r*cell,Math.max(9,cell/2),'transparent','#81b64c',3);}
  if(state.winLine){const [r1,c1,r2,c2]=state.winLine;ctx.beginPath();ctx.moveTo(margin+c1*cell,margin+r1*cell);ctx.lineTo(margin+c2*cell,margin+r2*cell);ctx.strokeStyle='#e02b2b';ctx.lineWidth=Math.max(5,cell/6);ctx.lineCap='round';ctx.stroke();ctx.lineCap='butt';}
}

function renderLog() {
  const log=$('#moveLog'); log.innerHTML='';
  state.moves.forEach((move,index)=>{const li=document.createElement('li');const col=String.fromCharCode(65+move.c);li.innerHTML=`<strong>${move.p===1?'B':'W'} ${col}${state.size-move.r}</strong>${move.opening?' · opening':`<em>${move.ms}ms</em>`}`;log.append(li);});
  log.scrollTop=log.scrollHeight;
}
function render() {
  fillSelects();
  blackEngine.value=state.blackEngine;whiteEngine.value=state.whiteEngine;blackLevel.value=state.blackLevel;whiteLevel.value=state.whiteLevel;randomPlies.value=state.randomPlies;pliesValue.value=state.randomPlies;
  const setup=state.phase==='setup', custom=state.openingMode==='custom';
  for(const control of [blackEngine,whiteEngine,blackLevel,whiteLevel,randomPlies]) control.disabled=!setup;
  document.querySelectorAll('[data-mode]').forEach(button=>{button.classList.toggle('selected',button.dataset.mode===state.openingMode);button.disabled=!setup;});
  rangeRow.hidden=custom;randomPlies.hidden=custom;undoOpening.hidden=!custom;undoOpening.disabled=!setup||!state.moves.length;
  startButton.disabled=!setup;startButton.hidden=!setup;resetButton.textContent=setup?'RESET':'RESET MATCH';
  board.classList.toggle('editable',setup&&custom);
  $('#openingHint').textContent=custom?(setup?'Left-click to place · right-click to undo':'Custom opening locked'):'Random opening is generated when the match starts.';
  $('#status').textContent=state.message;$('#moveCount').textContent=`${state.moves.length} MOVES`;$('#liveDot').classList.toggle('running',state.phase==='running');
  $('#blackState').textContent=state.thinkingPlayer===1?'CALCULATING…':state.winner===1?'WINNER':'';$('#whiteState').textContent=state.thinkingPlayer===2?'CALCULATING…':state.winner===2?'WINNER':'';
  const unavailable=state.engines.filter(engine=>!engine.available).map(engine=>engine.name);
  $('#runtimeNote').textContent=`Current v3 threads: ${state.threads||'auto'} · `+(unavailable.length?`Native runtimes missing for: ${unavailable.join(', ')}. Build them with scripts/build_third_party_engines.sh.`:'All external engines available via native Gomocup protocol adapters.');
  renderLog();drawBoard();
}
async function saveConfig(patch={}) {
  const current=configDraft||{blackEngine:state.blackEngine,whiteEngine:state.whiteEngine,blackLevel:state.blackLevel,whiteLevel:state.whiteLevel,openingMode:state.openingMode,randomPlies:state.randomPlies};
  configDraft={...current,blackEngine:+blackEngine.value,whiteEngine:+whiteEngine.value,blackLevel:+blackLevel.value,whiteLevel:+whiteLevel.value,randomPlies:+randomPlies.value,...patch};
  await post('/api/match/config',configDraft);
}
for(const select of [blackEngine,whiteEngine,blackLevel,whiteLevel])select.addEventListener('change',()=>saveConfig());
randomPlies.addEventListener('input',()=>pliesValue.value=randomPlies.value);randomPlies.addEventListener('change',()=>saveConfig());
document.querySelector('.segmented').addEventListener('click',event=>{const mode=event.target.dataset.mode;if(mode)saveConfig({openingMode:mode});});
undoOpening.addEventListener('click',()=>post('/api/match/undo-opening'));startButton.addEventListener('click',()=>post('/api/match/start'));resetButton.addEventListener('click',()=>post('/api/match/reset'));
board.addEventListener('pointerup',event=>{if(!state||state.phase!=='setup'||state.openingMode!=='custom')return;const {rect,margin,cell}=metrics(),c=Math.round((event.clientX-rect.left-margin)/cell),r=Math.round((event.clientY-rect.top-margin)/cell);if(r>=0&&c>=0&&r<state.size&&c<state.size)post('/api/match/opening',{r,c});});
board.addEventListener('contextmenu',event=>{event.preventDefault();if(state?.phase==='setup'&&state.openingMode==='custom')post('/api/match/undo-opening');});
document.addEventListener('keydown',event=>{if(event.target.matches('select,input'))return;const key=event.key.toLowerCase();if(key==='enter'&&state?.phase==='setup')post('/api/match/start');else if(key==='r')post('/api/match/reset');else if(key==='u')post('/api/match/undo-opening');else if(key==='q')request('/api/match/quit',{method:'POST'}).then(()=>$('#status').textContent='SERVER STOPPED');});
new ResizeObserver(drawBoard).observe(document.querySelector('.arena'));
async function refresh(){if(fetching)return;fetching=true;try{const next=await request('/api/match/state');const changed=!state||next.revision>state.revision;if(changed){state=next;render();}}catch{ $('#status').textContent='CONNECTION LOST'; }finally{fetching=false;}}
refresh();setInterval(refresh,250);
