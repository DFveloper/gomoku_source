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
const appBasePath = new URL(document.currentScript.src).pathname.replace(/\/app\.js$/, '');

function appUrl(path) {
  return `${appBasePath}${path.startsWith('/') ? path : `/${path}`}`;
}

let state = null;
let fetching = false;
let toastTimer = 0;
let configQueue = Promise.resolve();

const qualityColors = {
  'Brilliant': '#23a7a0', 'Great': '#5c9e46', 'Best': '#81b64c', 'Excellent': '#6aa94f',
  'Good': '#96a96c', 'Book': '#b69b72', 'Inaccuracy': '#f0b536', 'Mistake': '#e8892e',
  'Blunder': '#d94b45', 'Missed Win': '#d94b45'
};

function toast(message) {
  toastEl.textContent = message;
  toastEl.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { toastEl.hidden = true; }, 1800);
}

async function request(path, options = {}) {
  const response = await fetch(appUrl(path), options);
  const type = response.headers.get('content-type') || '';
  const result = type.includes('json') ? await response.json() : await response.text();
  if (!response.ok) throw new Error(result.error || result || 'Request failed');
  return result;
}

async function refresh() {
  if (fetching) return;
  fetching = true;
  try {
    const next = await request('/api/state');
    const currentForbidden = state?.forbidden?.join(',') || '';
    const nextForbidden = next.forbidden?.join(',') || '';
    const changed = !state || next.revision > state.revision || nextForbidden !== currentForbidden;
    if (!state || next.revision >= state.revision) {
      state = next;
      if (changed) render();
    }
  } catch (error) {
    statusEl.textContent = 'CONNECTION LOST';
    spinnerEl.hidden = true;
  } finally { fetching = false; }
}

async function action(name) {
  try {
    const next = await request('/api/action', {
      method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({action: name})
    });
    if (!state || next.revision >= state.revision) { state = next; render(); }
  } catch (error) { toast(error.message); }
}

function configure(payload) {
  configQueue = configQueue.then(async () => {
    try {
      const next = await request('/api/config', {
        method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(payload)
      });
      if (!state || next.revision >= state.revision) { state = next; render(); }
    } catch (error) { toast(error.message); render(); }
  });
  return configQueue;
}

function renderSetup() {
  const engines = state.engines || [];
  if (engineSelect.options.length !== engines.length) {
    engineSelect.innerHTML = engines.map(engine =>
      `<option value="${engine.id}"${engine.available ? '' : ' disabled'}>${engine.name}${engine.available ? '' : ' · NOT FOUND'}</option>`
    ).join('');
  }
  if (engineSelect.value !== String(state.engine)) engineSelect.value = String(state.engine);
  const maxLevel = state.maxLevel || 5;
  if (levelSelect.options.length !== maxLevel) {
    levelSelect.innerHTML = Array.from({length: maxLevel}, (_, i) =>
      `<option value="${i + 1}">LEVEL ${i + 1}</option>`).join('');
  }
  if (levelSelect.value !== String(state.level)) levelSelect.value = String(state.level);
}

function resizeCanvas(canvas, ctx) {
  const rect = canvas.getBoundingClientRect();
  const ratio = Math.min(devicePixelRatio || 1, 2);
  const width = Math.max(1, Math.round(rect.width * ratio));
  const height = Math.max(1, Math.round(rect.height * ratio));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width; canvas.height = height;
  }
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  return rect;
}

function boardMetrics() {
  const rect = boardCanvas.getBoundingClientRect();
  const size = Math.min(rect.width, rect.height);
  const margin = Math.max(18, size * .055);
  return {rect, size, margin, cell: (size - margin * 2) / (state.size - 1)};
}

function circle(ctx, x, y, radius, fill, stroke, lineWidth = 1) {
  ctx.beginPath(); ctx.arc(x, y, radius, 0, Math.PI * 2);
  ctx.fillStyle = fill; ctx.fill();
  if (stroke) { ctx.lineWidth = lineWidth; ctx.strokeStyle = stroke; ctx.stroke(); }
}

function drawBoard() {
  if (!state) return;
  const rect = resizeCanvas(boardCanvas, boardCtx);
  const {size, margin, cell} = boardMetrics();
  const ctx = boardCtx;
  ctx.clearRect(0, 0, rect.width, rect.height);
  const gradient = ctx.createLinearGradient(0, 0, size, size);
  gradient.addColorStop(0, '#f1c681'); gradient.addColorStop(.55, '#ebbb72'); gradient.addColorStop(1, '#dda55e');
  ctx.fillStyle = gradient; ctx.fillRect(0, 0, size, size);
  ctx.strokeStyle = '#563e28'; ctx.lineWidth = 1;
  for (let i = 0; i < state.size; i++) {
    const p = margin + i * cell;
    ctx.beginPath(); ctx.moveTo(margin, p); ctx.lineTo(size - margin, p); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(p, margin); ctx.lineTo(p, size - margin); ctx.stroke();
  }

  if (state.heat) for (const [r, c, score] of state.heatmap) {
    const x = margin + c * cell, y = margin + r * cell;
    circle(ctx, x, y, Math.max(3, cell / 4), `rgb(${230-score/2},${170+score/3},35)`);
  }
  if (state.size >= 13) {
    const points = [3, Math.floor(state.size / 2), state.size - 4];
    for (const r of points) for (const c of points)
      circle(ctx, margin + c * cell, margin + r * cell, Math.max(3, size * .0047), '#4d3b25');
  }

  for (let r = 0; r < state.size; r++) for (let c = 0; c < state.size; c++) {
    const stone = state.board[r][c];
    if (!stone) continue;
    const x = margin + c * cell, y = margin + r * cell;
    const radius = Math.max(7, cell / 2 - 3);
    const shade = ctx.createRadialGradient(x - radius * .35, y - radius * .45, 1, x, y, radius);
    if (stone === 1) { shade.addColorStop(0, '#414441'); shade.addColorStop(.45, '#202220'); shade.addColorStop(1, '#0b0c0b'); }
    else { shade.addColorStop(0, '#fffef7'); shade.addColorStop(.65, '#f2eee1'); shade.addColorStop(1, '#c7c1b3'); }
    circle(ctx, x, y, radius, shade, stone === 1 ? '#080808' : '#a09a8c');
  }

  if (state.forbidden) {
    const [r, c] = state.forbidden;
    const pulse = .5 + .5 * Math.sin(performance.now() / 55);
    circle(ctx, margin + c * cell, margin + r * cell, Math.max(8, cell / 2 - 3) + pulse * 4,
      `rgb(${170 + 70 * pulse},22,24)`, `rgb(255,${190 + 50 * pulse},${190 + 50 * pulse})`, 2);
    requestAnimationFrame(drawBoard);
  }

  if (state.last && state.last[0] >= 0) {
    const [r, c] = state.last;
    circle(ctx, margin + c * cell, margin + r * cell, Math.max(9, cell / 2), 'transparent', '#81b64c', 3);
  }
  if (state.winLine) {
    const [r1, c1, r2, c2] = state.winLine;
    ctx.beginPath(); ctx.moveTo(margin + c1 * cell, margin + r1 * cell); ctx.lineTo(margin + c2 * cell, margin + r2 * cell);
    ctx.strokeStyle = '#e02626'; ctx.lineWidth = Math.max(5, cell / 6); ctx.lineCap = 'round'; ctx.stroke(); ctx.lineCap = 'butt';
  }
}

function drawGraph() {
  if (!state?.reviewMode) return;
  const rect = resizeCanvas(graphCanvas, graphCtx);
  const ctx = graphCtx, w = rect.width, h = rect.height;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = '#31322f'; ctx.fillRect(0, 0, w, h / 2);
  ctx.fillStyle = '#e1e0d6'; ctx.fillRect(0, h / 2, w, h / 2);
  for (const pct of [25, 50, 75]) {
    const y = h - pct * h / 100;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y);
    ctx.setLineDash(pct === 50 ? [] : [4, 4]); ctx.lineWidth = pct === 50 ? 2 : 1;
    ctx.strokeStyle = pct === 50 ? '#81b64c' : '#878982'; ctx.stroke();
  }
  ctx.setLineDash([]);
  const reviews = state.reviews;
  const point = (item, i) => ({x: reviews.length === 1 ? w / 2 : i * w / (reviews.length - 1), y: h - item.winrate * h / 100});
  if (reviews.length > 1) {
    ctx.beginPath();
    reviews.forEach((item, i) => { const p = point(item, i); i ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y); });
    ctx.strokeStyle = '#f0b536'; ctx.lineWidth = 3; ctx.lineJoin = 'round'; ctx.stroke();
  }
  reviews.forEach((item, i) => {
    if (!['Inaccuracy','Mistake','Blunder','Brilliant'].includes(item.label)) return;
    const p = point(item, i); circle(ctx, p.x, p.y, 4, qualityColors[item.label], '#fff');
  });
  ctx.font = '800 12px system-ui';
  ctx.fillStyle = '#f5f4e8'; ctx.fillText('B', 6, 15); ctx.fillText('50', 6, h/2 - 5);
  ctx.fillStyle = '#263126'; ctx.fillText('W', 6, h - 6);
  if (reviews.length && state.reviewPly > 0) {
    const index = Math.min(state.reviewPly - 1, reviews.length - 1), p = point(reviews[index], index);
    ctx.setLineDash([3,3]); ctx.beginPath(); ctx.moveTo(p.x, 0); ctx.lineTo(p.x, h); ctx.strokeStyle = '#fff'; ctx.lineWidth = 1; ctx.stroke(); ctx.setLineDash([]);
    circle(ctx, p.x, p.y, 5, '#f0b536', '#fff');
    const label = `B ${Math.round(reviews[index].winrate)}%  #${index + 1}`;
    ctx.font = '800 11px system-ui'; const tw = ctx.measureText(label).width + 14;
    ctx.fillStyle = '#1b1d1b'; ctx.fillRect(w - tw - 7, 7, tw, 23);
    ctx.fillStyle = '#fff'; ctx.fillText(label, w - tw, 22);
  }
}

function button(label, name, extra = '') {
  return `<button class="action ${extra}" type="button" data-action="${name}">${label}</button>`;
}

function renderActions() {
  if (state.reviewMode) {
    actionsEl.className = 'actions review-actions';
    actionsEl.innerHTML = button('PREV', 'prev') + button('NEXT', 'next') + button('END', 'end') +
      button('SAVE', 'replay') + button('LOAD', 'load') + button('NEW', 'new');
  } else {
    actionsEl.className = 'actions';
    actionsEl.innerHTML = button('NEW', 'new') + button('UNDO', 'undo') +
      button(state.heat ? 'HEAT ON' : 'HEAT OFF', 'heat', state.heat ? 'active' : '') +
      button('SAVE', 'log') + button(`SIZE ${state.size}`, 'size');
  }
  for (const item of actionsEl.querySelectorAll('button')) {
    if (state.aiThinking && !['new'].includes(item.dataset.action)) item.disabled = true;
  }
}

function renderReview() {
  reviewEl.hidden = !state.reviewMode;
  if (!state.reviewMode) return;
  plyEl.textContent = `PLY ${state.reviewPly}/${state.moves.length}`;
  const move = state.reviewPly > 0 ? state.reviews[state.reviewPly - 1] : null;
  if (!move) reviewResultEl.innerHTML = '';
  else {
    const label = move.label === 'Missed Win' ? 'Miss' : move.label;
    const color = qualityColors[move.label] || '#96a96c';
    reviewResultEl.innerHTML = `<span class="quality" style="color:${color}"><i class="badge" style="background:${color}">★</i>${label}</span><span class="winrate">BLACK WIN · ${Math.round(move.winrate)}%</span>`;
  }
  drawGraph();
}

function render() {
  if (!state) return;
  statusEl.textContent = state.status;
  movesEl.textContent = `${state.moves.length} MOVE${state.moves.length === 1 ? '' : 'S'}`;
  spinnerEl.hidden = !state.aiThinking;
  renderSetup(); renderActions(); renderReview(); drawBoard();
}

boardCanvas.addEventListener('pointerup', async event => {
  if (!state || state.aiThinking || state.reviewMode || state.gameOver) return;
  const {rect, margin, cell} = boardMetrics();
  const c = Math.round((event.clientX - rect.left - margin) / cell);
  const r = Math.round((event.clientY - rect.top - margin) / cell);
  const x = margin + c * cell, y = margin + r * cell;
  if (r < 0 || c < 0 || r >= state.size || c >= state.size || Math.hypot(event.clientX - rect.left - x, event.clientY - rect.top - y) > Math.max(18, cell / 2)) return;
  const previous = state;
  const board = state.board.map(row => row.slice());
  board[r][c] = 1;
  state = {...state, board, moves: [...state.moves, [r, c]], last: [r, c], aiThinking: true, status: 'CALCULATING...'};
  render();
  try {
    const next = await request('/api/move', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({r,c})});
    if (!state || next.revision >= state.revision) { state = next; render(); }
  } catch (error) {
    state = previous;
    render();
    toast(error.message === 'forbidden move' ? 'Forbidden move for black' : error.message);
    refresh();
  }
});

actionsEl.addEventListener('click', event => {
  const name = event.target.closest('button')?.dataset.action;
  if (!name) return;
  if (['new','undo','heat','size','prev','next','end'].includes(name)) action(name);
  else if (name === 'log') window.location.assign(appUrl('/api/log'));
  else if (name === 'replay') window.location.assign(appUrl('/api/replay'));
  else if (name === 'load') replayFileEl.click();
});

replayFileEl.addEventListener('change', async () => {
  const file = replayFileEl.files[0];
  if (!file) return;
  try {
    const next = await request('/api/load', {method:'POST', headers:{'Content-Type':'text/plain'}, body:await file.text()});
    if (!state || next.revision >= state.revision) { state = next; render(); }
    toast('Replay loaded');
  } catch (error) { toast('Invalid replay file'); }
  replayFileEl.value = '';
});

function applyAiSettings() {
  configure({
    engine: Number(engineSelect.value),
    level: Number(levelSelect.value)
  });
}

engineSelect.addEventListener('change', applyAiSettings);
levelSelect.addEventListener('change', applyAiSettings);

document.addEventListener('keydown', event => {
  if (event.ctrlKey || event.metaKey || event.altKey || event.target.matches('input')) return;
  const key = event.key.toLowerCase();
  if (state?.reviewMode) {
    if (key === 'a') action('prev'); else if (key === 'd') action('next'); else if (key === 'e') action('end');
    else if (key === 's') window.location.assign(appUrl('/api/replay')); else if (key === 'l') replayFileEl.click(); else if (key === 'n') action('new');
  } else {
    if (key === 'n') action('new'); else if (key === 'u') action('undo'); else if (key === 'd') action('heat');
    else if (key === 's') window.location.assign(appUrl('/api/log')); else if (key === 'l') replayFileEl.click();
  }
  if (key === 'q') request('/api/quit', {method:'POST'})
    .then(() => { statusEl.textContent = 'SERVER STOPPED'; })
    .catch(error => toast(error.message));
});

new ResizeObserver(() => { drawBoard(); drawGraph(); }).observe(document.querySelector('.app'));
refresh();
setInterval(refresh, 250);
