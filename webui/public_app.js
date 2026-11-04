const boardCanvas = document.querySelector('#board');
const boardCtx = boardCanvas.getContext('2d');
const graphCanvas = document.querySelector('#graph');
const graphCtx = graphCanvas.getContext('2d');
const actionsEl = document.querySelector('#actions');
const statusEl = document.querySelector('#status');
const movesEl = document.querySelector('#moves');
const spinnerEl = document.querySelector('#spinner');
const reviewEl = document.querySelector('#review');
const plyEl = document.querySelector('#ply');
const reviewResultEl = document.querySelector('#reviewResult');
const replayFileEl = document.querySelector('#replayFile');
const toastEl = document.querySelector('#toast');
const engineSelect = document.querySelector('#engineSelect');
const levelSelect = document.querySelector('#levelSelect');
const sanginMode = document.documentElement.dataset.profile === 'sangin';
const appBasePath = new URL('.', document.location.href).pathname.replace(/\/$/, '');
const DB_NAME = sanginMode ? 'aeonthic-sangin-browser' : 'aeonthic-gomoku-browser';
const DB_STORE = 'games';
const DB_KEY = 'current';
const SANGIN_ENGINE = 4;

document.cookie = `${sanginMode?'sangin':'gomoku'}_storage=browser-only; Path=${appBasePath || '/'}; Max-Age=31536000; SameSite=Lax; Secure`;

let model;
let state;
let capabilities = {engines: [], maxLevel: 5};
let aiThinking = false;
let forbidden = null;
let toastTimer = 0;
let operationId = 0;

const qualityColors = {
  Brilliant:'#23a7a0', Great:'#5c9e46', Best:'#81b64c', Excellent:'#6aa94f', Good:'#96a96c',
  Book:'#b69b72', Inaccuracy:'#f0b536', Mistake:'#e8892e', Blunder:'#d94b45', 'Missed Win':'#d94b45'
};

function appUrl(path) { return `${appBasePath}${path.startsWith('/') ? path : `/${path}`}`; }
function toast(message) { toastEl.textContent=message;toastEl.hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>toastEl.hidden=true,1800); }
async function api(path, body) {
  const response = await fetch(appUrl(path), {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || 'Request failed');
  return result;
}

function openBrowserDb() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, 1);
    request.onupgradeneeded = () => request.result.createObjectStore(DB_STORE);
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

async function readBrowserRecord() {
  try {
    const db=await openBrowserDb();
    return await new Promise((resolve,reject)=>{const tx=db.transaction(DB_STORE,'readonly');const req=tx.objectStore(DB_STORE).get(DB_KEY);req.onsuccess=()=>resolve(req.result);req.onerror=()=>reject(req.error);});
  } catch { const value=localStorage.getItem(DB_NAME); return value ? JSON.parse(value) : null; }
}

async function writeBrowserRecord(value) {
  try {
    const db=await openBrowserDb();
    await new Promise((resolve,reject)=>{const tx=db.transaction(DB_STORE,'readwrite');tx.objectStore(DB_STORE).put(value,DB_KEY);tx.oncomplete=resolve;tx.onerror=()=>reject(tx.error);});
  } catch { localStorage.setItem(DB_NAME,JSON.stringify(value)); }
}

function freshModel(previous={}) {
  return {version:1,size:sanginMode?15:(previous.size||15),moves:[],heat:sanginMode?false:Boolean(previous.heat),heatmap:[],gameOver:false,winner:0,
    reviews:[],reviewMode:false,reviewPly:0,engine:sanginMode?SANGIN_ENGINE:(Number.isInteger(previous.engine)?previous.engine:2),
    level:sanginMode?5:(Number.isInteger(previous.level)?previous.level:4),winLine:null,revision:(previous.revision||0)+1};
}

function normalizeModel(value) {
  const base=freshModel(value||{}), size=sanginMode?15:([13,15,19].includes(value?.size)?value.size:15);
  const moves=Array.isArray(value?.moves)?value.moves.filter(move=>Array.isArray(move)&&move.length===2&&move.every(Number.isInteger)&&move[0]>=0&&move[0]<size&&move[1]>=0&&move[1]<size):[];
  return {...base,...value,size,moves,heat:sanginMode?false:Boolean(value?.heat),engine:sanginMode?SANGIN_ENGINE:base.engine,
    level:sanginMode?5:base.level,heatmap:sanginMode?[]:(Array.isArray(value?.heatmap)?value.heatmap:[]),
    reviews:Array.isArray(value?.reviews)?value.reviews:[],revision:Number(value?.revision)||1};
}

function makeView() {
  const shown=model.reviewMode?Math.min(model.reviewPly,model.moves.length):model.moves.length;
  const board=Array.from({length:model.size},()=>Array(model.size).fill(0));
  model.moves.slice(0,shown).forEach(([r,c],i)=>board[r][c]=i%2===0?1:2);
  const last=shown?model.moves[shown-1]:[-1,-1];
  const status=sanginMode?(model.reviewMode?'복기':model.gameOver?(model.winner===0?'무승부':model.winner===1?'승리':'패배'):aiThinking?'계산 중…':'내 차례'):
    (model.reviewMode?'GAME REVIEW':model.gameOver?(model.winner===0?'DRAW':model.winner===1?'YOU WIN!':'AI WINS!'):aiThinking?'CALCULATING...':'YOUR TURN');
  return {...model,board,last,status,aiThinking,forbidden,engines:capabilities.engines,maxLevel:capabilities.maxLevel,
    heatmap:model.heat&&!model.reviewMode?model.heatmap:[],winLine:model.reviewMode&&shown!==model.moves.length?null:model.winLine};
}

function commit(renderNow=true) { model.revision=(model.revision||0)+1;writeBrowserRecord(model);state=makeView();if(renderNow)render(); }
async function loadCapabilities() { capabilities=await api('/api/public/capabilities',{size:model.size});if(sanginMode){model.size=15;model.engine=SANGIN_ENGINE;model.level=5;model.heat=false;}else if(!capabilities.engines.some(e=>e.id===model.engine&&e.available))model.engine=2; }
async function refreshHeatmap() { model.heatmap=!sanginMode&&model.heat&&!model.reviewMode?(await api('/api/public/heatmap',{size:model.size,moves:model.moves})).heatmap:[]; }

function resizeCanvas(canvas,ctx){const rect=canvas.getBoundingClientRect(),ratio=Math.min(devicePixelRatio||1,2),w=Math.max(1,Math.round(rect.width*ratio)),h=Math.max(1,Math.round(rect.height*ratio));if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;}ctx.setTransform(ratio,0,0,ratio,0,0);return rect;}
function boardMetrics(){const rect=boardCanvas.getBoundingClientRect(),size=Math.min(rect.width,rect.height),margin=Math.max(18,size*.055);return{rect,size,margin,cell:(size-margin*2)/(state.size-1)};}
function circle(ctx,x,y,r,fill,stroke,width=1){ctx.beginPath();ctx.arc(x,y,r,0,Math.PI*2);ctx.fillStyle=fill;ctx.fill();if(stroke){ctx.lineWidth=width;ctx.strokeStyle=stroke;ctx.stroke();}}
function drawBoard(){if(!state)return;const rect=resizeCanvas(boardCanvas,boardCtx),{size,margin,cell}=boardMetrics(),ctx=boardCtx;ctx.clearRect(0,0,rect.width,rect.height);const gradient=ctx.createLinearGradient(0,0,size,size);gradient.addColorStop(0,'#f1c681');gradient.addColorStop(.55,'#ebbb72');gradient.addColorStop(1,'#dda55e');ctx.fillStyle=gradient;ctx.fillRect(0,0,size,size);ctx.strokeStyle='#563e28';ctx.lineWidth=1;for(let i=0;i<state.size;i++){const p=margin+i*cell;ctx.beginPath();ctx.moveTo(margin,p);ctx.lineTo(size-margin,p);ctx.stroke();ctx.beginPath();ctx.moveTo(p,margin);ctx.lineTo(p,size-margin);ctx.stroke();}if(state.heat)for(const[r,c,score]of state.heatmap)circle(ctx,margin+c*cell,margin+r*cell,Math.max(3,cell/4),`rgb(${230-score/2},${170+score/3},35)`);if(state.size>=13){const points=[3,Math.floor(state.size/2),state.size-4];for(const r of points)for(const c of points)circle(ctx,margin+c*cell,margin+r*cell,Math.max(3,size*.0047),'#4d3b25');}for(let r=0;r<state.size;r++)for(let c=0;c<state.size;c++){const stone=state.board[r][c];if(!stone)continue;const x=margin+c*cell,y=margin+r*cell,rad=Math.max(7,cell/2-3),shade=ctx.createRadialGradient(x-rad*.35,y-rad*.45,1,x,y,rad);if(stone===1){shade.addColorStop(0,'#414441');shade.addColorStop(.45,'#202220');shade.addColorStop(1,'#0b0c0b');}else{shade.addColorStop(0,'#fffef7');shade.addColorStop(.65,'#f2eee1');shade.addColorStop(1,'#c7c1b3');}circle(ctx,x,y,rad,shade,stone===1?'#080808':'#a09a8c');}if(state.forbidden){const[r,c]=state.forbidden;circle(ctx,margin+c*cell,margin+r*cell,Math.max(8,cell/2),'#b51618','#ffd0d0',2);}if(state.last?.[0]>=0){const[r,c]=state.last;circle(ctx,margin+c*cell,margin+r*cell,Math.max(9,cell/2),'transparent','#81b64c',3);}if(state.winLine){const[r1,c1,r2,c2]=state.winLine;ctx.beginPath();ctx.moveTo(margin+c1*cell,margin+r1*cell);ctx.lineTo(margin+c2*cell,margin+r2*cell);ctx.strokeStyle='#e02626';ctx.lineWidth=Math.max(5,cell/6);ctx.lineCap='round';ctx.stroke();ctx.lineCap='butt';}}
function drawGraph(){if(!state?.reviewMode)return;const rect=resizeCanvas(graphCanvas,graphCtx),ctx=graphCtx,w=rect.width,h=rect.height;ctx.clearRect(0,0,w,h);ctx.fillStyle='#31322f';ctx.fillRect(0,0,w,h/2);ctx.fillStyle='#e1e0d6';ctx.fillRect(0,h/2,w,h/2);for(const pct of[25,50,75]){const y=h-pct*h/100;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.setLineDash(pct===50?[]:[4,4]);ctx.strokeStyle=pct===50?'#81b64c':'#878982';ctx.stroke();}ctx.setLineDash([]);const reviews=state.reviews,point=(item,i)=>({x:reviews.length===1?w/2:i*w/(reviews.length-1),y:h-item.winrate*h/100});if(reviews.length>1){ctx.beginPath();reviews.forEach((item,i)=>{const p=point(item,i);i?ctx.lineTo(p.x,p.y):ctx.moveTo(p.x,p.y);});ctx.strokeStyle='#f0b536';ctx.lineWidth=3;ctx.stroke();}reviews.forEach((item,i)=>{if(!['Inaccuracy','Mistake','Blunder','Brilliant'].includes(item.label))return;const p=point(item,i);circle(ctx,p.x,p.y,4,qualityColors[item.label],'#fff');});if(reviews.length&&state.reviewPly>0){const i=Math.min(state.reviewPly-1,reviews.length-1),p=point(reviews[i],i);circle(ctx,p.x,p.y,5,'#f0b536','#fff');}}
function button(label,name,extra=''){return`<button class="action ${extra}" type="button" data-action="${name}">${label}</button>`;}
function renderSetup(){if(sanginMode)return;const engines=state.engines||[];if(engineSelect.options.length!==engines.length)engineSelect.innerHTML=engines.map(e=>`<option value="${e.id}"${e.available?'':' disabled'}>${e.name}${e.available?'':' · NOT FOUND'}</option>`).join('');engineSelect.value=String(state.engine);if(levelSelect.options.length!==state.maxLevel)levelSelect.innerHTML=Array.from({length:state.maxLevel},(_,i)=>`<option value="${i+1}">LEVEL ${i+1}</option>`).join('');levelSelect.value=String(state.level);}
function renderActions(){if(sanginMode){actionsEl.className=state.reviewMode?'actions review-actions':'actions';actionsEl.innerHTML=state.reviewMode?button('이전','prev')+button('다음','next')+button('마지막','end')+button('새 대국','new'):button('새 대국','new');}else if(state.reviewMode){actionsEl.className='actions review-actions';actionsEl.innerHTML=button('PREV','prev')+button('NEXT','next')+button('END','end')+button('SAVE','replay')+button('LOAD','load')+button('NEW','new');}else{actionsEl.className='actions';actionsEl.innerHTML=button('NEW','new')+button('UNDO','undo')+button(state.heat?'HEAT ON':'HEAT OFF','heat',state.heat?'active':'')+button('SAVE','log')+button(`SIZE ${state.size}`,'size');}for(const item of actionsEl.querySelectorAll('button'))if(state.aiThinking&&item.dataset.action!=='new')item.disabled=true;}
function renderReview(){reviewEl.hidden=!state.reviewMode;if(!state.reviewMode)return;plyEl.textContent=`PLY ${state.reviewPly}/${state.moves.length}`;const move=state.reviewPly>0?state.reviews[state.reviewPly-1]:null;if(!move)reviewResultEl.innerHTML='';else{const color=qualityColors[move.label]||'#96a96c';reviewResultEl.innerHTML=`<span class="quality" style="color:${color}"><i class="badge" style="background:${color}">★</i>${move.label}</span><span class="winrate">BLACK WIN · ${Math.round(move.winrate)}%</span>`;}drawGraph();}
function render(){state=makeView();statusEl.textContent=state.status;movesEl.textContent=sanginMode?`${state.moves.length}수`:`${state.moves.length} MOVE${state.moves.length===1?'':'S'}`;spinnerEl.hidden=!state.aiThinking;renderSetup();renderActions();renderReview();drawBoard();}

async function action(name){operationId++;if(name==='new'){model=freshModel(model);if(sanginMode)showRules();}else if(name==='undo'&&!model.gameOver){model.moves.splice(Math.max(0,model.moves.length-2));model.winLine=null;model.reviews=[];model.heatmap=[];}else if(name==='heat'&&!sanginMode){model.heat=!model.heat;await refreshHeatmap();}else if(name==='size'&&!sanginMode&&!model.moves.length){model.size=model.size===15?19:model.size===19?13:15;model=freshModel(model);await loadCapabilities();}else if(name==='prev')model.reviewPly=Math.max(0,model.reviewPly-1);else if(name==='next')model.reviewPly=Math.min(model.moves.length,model.reviewPly+1);else if(name==='end')model.reviewPly=model.moves.length;aiThinking=false;commit();}
async function configure(){model.engine=Number(engineSelect.value);model.level=Number(levelSelect.value);commit();}

boardCanvas.addEventListener('pointerup',async event=>{if(!state||aiThinking||model.reviewMode||model.gameOver)return;const{rect,margin,cell}=boardMetrics(),c=Math.round((event.clientX-rect.left-margin)/cell),r=Math.round((event.clientY-rect.top-margin)/cell),x=margin+c*cell,y=margin+r*cell;if(r<0||c<0||r>=state.size||c>=state.size||Math.hypot(event.clientX-rect.left-x,event.clientY-rect.top-y)>Math.max(18,cell/2)||state.board[r][c])return;const id=++operationId,previousMoves=model.moves.slice();model.moves.push([r,c]);aiThinking=true;render();try{const result=await api('/api/public/move',{size:model.size,moves:previousMoves,engine:model.engine,level:model.level,r,c});if(id!==operationId)return;if(result.aiMove)model.moves.push(result.aiMove);model.gameOver=result.gameOver;model.winner=result.winner;model.winLine=result.winLine;model.reviews=result.reviews||[];model.reviewMode=result.gameOver;model.reviewPly=model.moves.length;await refreshHeatmap();}catch(error){if(id!==operationId)return;model.moves=previousMoves;if(error.message==='forbidden move'){forbidden=[r,c];toast(sanginMode?'흑 금수입니다 (장목·3-3·4-4)':'Forbidden move for black');setTimeout(()=>{forbidden=null;render();},700);}else toast(error.message);}finally{if(id===operationId){aiThinking=false;commit();}}});

function download(name,text,type='text/plain'){const url=URL.createObjectURL(new Blob([text],{type}));const link=document.createElement('a');link.href=url;link.download=name;link.click();setTimeout(()=>URL.revokeObjectURL(url),0);}
function replayText(){return`GOMOKU_REPLAY_V1 ${model.size} ${model.winner} BROWSER_ONLY\n`+model.moves.map(move=>move.join(' ')).join('\n')+'\n';}
function logText(){const board=Array.from({length:model.size},()=>Array(model.size).fill(0));let out='--- Gomoku browser-local review ---\n';model.moves.forEach(([r,c],i)=>{board[r][c]=i%2===0?1:2;out+=`\n[${i+1}] ${i%2===0?'Human':'AI'} (${r}, ${c})\n`+board.map(row=>row.map(v=>v===1?'X':v===2?'O':'.').join(' ')).join('\n')+'\n';});return out;}
actionsEl.addEventListener('click',event=>{const name=event.target.closest('button')?.dataset.action;if(!name)return;if(['new','undo','heat','size','prev','next','end'].includes(name))action(name);else if(name==='log')download('gomoku_log.txt',logText());else if(name==='replay')download('gomoku_replay.txt',replayText());else if(name==='load')replayFileEl.click();});
replayFileEl.addEventListener('change',async()=>{const file=replayFileEl.files[0];if(!file)return;try{const lines=(await file.text()).trim().split(/\r?\n/),head=lines.shift().trim().split(/\s+/);if(head[0]!=='GOMOKU_REPLAY_V1')throw new Error();const size=Number(head[1]),moves=lines.filter(Boolean).map(line=>line.trim().split(/\s+/).map(Number));const result=await api('/api/public/review',{size,moves});model={...freshModel(model),size,moves,gameOver:result.gameOver,winner:result.winner,winLine:result.winLine,reviews:result.reviews,reviewMode:true,reviewPly:moves.length};await loadCapabilities();commit();toast('Replay loaded from this browser');}catch{toast('Invalid replay file');}replayFileEl.value='';});
engineSelect?.addEventListener('change',configure);levelSelect?.addEventListener('change',configure);
if(!sanginMode)document.addEventListener('keydown',event=>{if(event.ctrlKey||event.metaKey||event.altKey||event.target.matches('input'))return;const key=event.key.toLowerCase();if(state?.reviewMode){if(key==='a')action('prev');else if(key==='d')action('next');else if(key==='e')action('end');else if(key==='s')download('gomoku_replay.txt',replayText());else if(key==='l')replayFileEl.click();else if(key==='n')action('new');}else{if(key==='n')action('new');else if(key==='u')action('undo');else if(key==='d')action('heat');else if(key==='s')download('gomoku_log.txt',logText());else if(key==='l')replayFileEl.click();}if(key==='q')toast('Public mode keeps the shared server running');});
new ResizeObserver(()=>{drawBoard();drawGraph();}).observe(document.querySelector('.app'));

function showRules(){const dialog=document.querySelector('#rulesDialog');if(dialog){dialog.hidden=false;document.querySelector('#rulesStart')?.focus();}}
document.querySelector('#rulesStart')?.addEventListener('click',()=>{document.querySelector('#rulesDialog').hidden=true;boardCanvas.focus();});

(async()=>{model=normalizeModel(await readBrowserRecord());await loadCapabilities();await refreshHeatmap();commit();if(sanginMode)showRules();})().catch(()=>{statusEl.textContent='CONNECTION LOST';spinnerEl.hidden=true;});
