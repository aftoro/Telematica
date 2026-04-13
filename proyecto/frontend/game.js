const screens = {
  login: document.getElementById('screen-login'),
  lobby: document.getElementById('screen-lobby'),
  game: document.getElementById('screen-game'),
};

const state = {
  token: null,
  name: null,
  role: null,
  room: null,
  players: [],
  resources: [],
  alerts: [],
  myPos: { x: 0, y: 0 },
  myPoints: 0,
};

const canvas = document.getElementById('game-canvas');
const ctx = canvas.getContext('2d');

let renderStarted = false;
let statusTimer = null;
let alertsTimer = null;

function showScreen(key) {
  Object.values(screens).forEach((s) => s.classList.remove('active'));
  screens[key].classList.add('active');
}

function setActionLine(text) {
  document.getElementById('action-line').textContent = text;
}

async function apiGet(path) {
  const res = await fetch(path);
  const text = await res.text();
  if (!res.ok) throw new Error(text || 'HTTP error');
  return text;
}

function parseJSON(text) {
  return JSON.parse(text);
}

async function login() {
  const name = document.getElementById('name-input').value.trim() || 'anon';
  const role = document.getElementById('role-select').value;
  const msg = document.getElementById('login-msg');

  try {
    const data = parseJSON(await apiGet(`/api/login?name=${encodeURIComponent(name)}&role=${encodeURIComponent(role)}`));
    state.token = data.token;
    state.name = data.name;
    state.role = data.role;
    msg.textContent = '';
    showScreen('lobby');
    await loadRooms();
  } catch (e) {
    msg.textContent = `Error login: ${e.message}`;
  }
}

async function loadRooms() {
  const roomsDiv = document.getElementById('rooms');
  const msg = document.getElementById('lobby-msg');

  try {
    const data = parseJSON(await apiGet('/api/lobby'));
    roomsDiv.innerHTML = '';

    data.rooms.forEach((room) => {
      const btn = document.createElement('button');
      btn.textContent = `Entrar en ${room.name}`;
      btn.onclick = () => joinRoom(room.id);
      roomsDiv.appendChild(btn);
    });

    msg.textContent = '';
  } catch (e) {
    msg.textContent = `Error lobby: ${e.message}`;
  }
}

async function joinRoom(roomId) {
  const msg = document.getElementById('lobby-msg');
  try {
    await apiGet(`/api/join?token=${encodeURIComponent(state.token)}&room=${encodeURIComponent(roomId)}`);
    state.room = roomId;
    document.getElementById('player-label').textContent = `${state.name} (${state.role})`;
    await refreshStatus();
    showScreen('game');
    startGameLoop();
  } catch (e) {
    msg.textContent = `No se pudo unir: ${e.message}`;
  }
}

async function sendCommand(cmd) {
  const text = await apiGet(`/api/command?token=${encodeURIComponent(state.token)}&cmd=${encodeURIComponent(cmd)}`);
  return text.trim();
}

function parseStatus(line) {
  const prefix = 'DATA STATUS PLAYERS ';
  if (!line.startsWith(prefix)) return;

  const rest = line.slice(prefix.length);
  const split = rest.split(' RESOURCES ');
  const pRaw = split[0] || '';
  const rRaw = split[1] || '';

  state.players = pRaw
    .split(',')
    .filter(Boolean)
    .map((item) => {
      const parts = item.split(':');
      if (parts.length < 5) return null;
      const [id, name, role, x, y, points] = parts;
      const parsed = { id: Number(id), name, role, x: Number(x), y: Number(y), points: Number(points ?? 0) };
      if (!Number.isFinite(parsed.x) || !Number.isFinite(parsed.y)) return null;
      if (!Number.isFinite(parsed.points)) parsed.points = 0;
      return parsed;
    })
    .filter(Boolean);

  state.resources = rRaw
    .split(',')
    .filter(Boolean)
    .map((item) => {
      const parts = item.split(':');
      if (parts.length < 5) return null;
      const [id, name, x, y, status] = parts;
      const parsed = { id: Number(id), name, x: Number(x), y: Number(y), status: Number(status) };
      if (!Number.isFinite(parsed.x) || !Number.isFinite(parsed.y)) return null;
      return parsed;
    })
    .filter(Boolean);

  const me = state.players.find((p) => p.name === state.name);
  if (me && Number.isFinite(me.x) && Number.isFinite(me.y)) {
    state.myPos.x = me.x;
    state.myPos.y = me.y;
    state.myPoints = Number.isFinite(me.points) ? me.points : state.myPoints;
  }
}

async function refreshStatus() {
  try {
    const line = await sendCommand('STATUS');
    parseStatus(line);
    document.getElementById('status-line').textContent = `Pos (${state.myPos.x}, ${state.myPos.y}) | Puntos ${state.myPoints}`;
  } catch (e) {
    document.getElementById('status-line').textContent = `Error STATUS: ${e.message}`;
  }
}

async function refreshAlerts() {
  try {
    const text = await apiGet(`/api/alerts?token=${encodeURIComponent(state.token)}`);
    if (text !== 'NONE') {
      const incoming = text.split('|').filter(Boolean);
      state.alerts.push(...incoming);
      state.alerts = state.alerts.slice(-4);
    }
    document.getElementById('alerts').textContent = state.alerts.join(' | ');
  } catch (_) {
    // noop
  }
}

function worldToCanvas(x, y) {
  return {
    x: (x / 100) * canvas.width,
    y: (y / 100) * canvas.height,
  };
}

function draw() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  ctx.fillStyle = '#111920';
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  for (let gx = 0; gx <= 10; gx++) {
    const x = (gx / 10) * canvas.width;
    ctx.strokeStyle = 'rgba(130,180,180,0.1)';
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, canvas.height);
    ctx.stroke();
  }

  for (let gy = 0; gy <= 10; gy++) {
    const y = (gy / 10) * canvas.height;
    ctx.strokeStyle = 'rgba(130,180,180,0.1)';
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(canvas.width, y);
    ctx.stroke();
  }

  state.resources.forEach((r) => {
    const p = worldToCanvas(r.x, r.y);
    const blink = Math.floor(Date.now() / 300) % 2 === 0;
    if (r.status === 1) {
      ctx.fillStyle = blink ? '#ff3b3b' : '#7f1111';
    } else if (r.status === 2) {
      ctx.fillStyle = '#8b1a1a';
    } else {
      ctx.fillStyle = '#3be46b';
    }
    ctx.fillRect(p.x - 8, p.y - 8, 16, 16);
    ctx.fillStyle = '#c8f5e7';
    ctx.font = '12px sans-serif';
    ctx.fillText(`#${r.id}`, p.x + 10, p.y - 4);
  });

  const drawPlayers = [...state.players];
  const hasLocal = drawPlayers.some((pl) => pl.name === state.name);
  if (!hasLocal && Number.isFinite(state.myPos.x) && Number.isFinite(state.myPos.y)) {
    drawPlayers.push({
      id: -1,
      name: state.name,
      role: state.role,
      x: state.myPos.x,
      y: state.myPos.y,
    });
  }

  drawPlayers.forEach((pl) => {
    const p = worldToCanvas(pl.x, pl.y);
    ctx.beginPath();
    ctx.arc(p.x, p.y, 9, 0, Math.PI * 2);
    if (pl.role === 'DEFENSOR') {
      ctx.fillStyle = '#4db2ff';
    } else {
      ctx.fillStyle = '#ffa74d';
    }
    if (pl.name === state.name) {
      ctx.strokeStyle = '#ffffff';
      ctx.lineWidth = 2;
      ctx.stroke();
    }
    ctx.fill();
    ctx.fillStyle = '#d9f7ed';
    ctx.fillText(pl.name, p.x + 12, p.y + 4);
  });
}

function nearestResourceId() {
  if (!state.resources.length) return null;
  let best = null;
  let bestDist = Infinity;
  for (const r of state.resources) {
    const dx = state.myPos.x - r.x;
    const dy = state.myPos.y - r.y;
    const dist = Math.sqrt(dx * dx + dy * dy);
    if (dist < bestDist) {
      bestDist = dist;
      best = r.id;
    }
  }
  return bestDist <= 12 ? best : null;
}

async function move(dx, dy) {
  const me = state.players.find((p) => p.name === state.name);
  const baseX = Number.isFinite(state.myPos.x) ? state.myPos.x : (me ? me.x : 0);
  const baseY = Number.isFinite(state.myPos.y) ? state.myPos.y : (me ? me.y : 0);

  const nx = Math.max(0, Math.min(100, baseX + dx));
  const ny = Math.max(0, Math.min(100, baseY + dy));
  const response = await sendCommand(`MOVE ${nx} ${ny}`);
  if (!response.startsWith('ERROR')) {
    state.myPos.x = nx;
    state.myPos.y = ny;

    const me = state.players.find((p) => p.name === state.name);
    if (me) {
      me.x = nx;
      me.y = ny;
    }

    draw();
    refreshStatus().catch(() => {});
  }
}

async function executeAction(command, successLabel) {
  try {
    const response = await sendCommand(command);
    setActionLine(`${successLabel}: ${response}`);
    await refreshStatus();
    return response;
  } catch (e) {
    setActionLine(`Error: ${e.message}`);
    return null;
  }
}

document.addEventListener('keydown', async (e) => {
  if (!screens.game.classList.contains('active')) return;

  if (['ArrowUp', 'w', 'W'].includes(e.key)) await move(0, -2);
  if (['ArrowDown', 's', 'S'].includes(e.key)) await move(0, 2);
  if (['ArrowLeft', 'a', 'A'].includes(e.key)) await move(-2, 0);
  if (['ArrowRight', 'd', 'D'].includes(e.key)) await move(2, 0);

  if (e.key === ' ') {
    await executeAction('SCAN', 'SCAN');
  }

  if (e.key.toLowerCase() === 'e' && state.role === 'ATACANTE') {
    const rid = nearestResourceId();
    if (rid != null) {
      await executeAction(`ATTACK ${rid}`, 'ATTACK');
    } else {
      setActionLine('ATTACK: no hay recurso cercano');
    }
  }

  if (e.key.toLowerCase() === 'q' && state.role === 'DEFENSOR') {
    const rid = nearestResourceId();
    if (rid != null) {
      await executeAction(`MITIGATE ${rid}`, 'MITIGATE');
    } else {
      setActionLine('MITIGATE: no hay recurso cercano');
    }
  }
});

function startGameLoop() {
  if (renderStarted) {
    return;
  }

  renderStarted = true;

  const renderFrame = () => {
    draw();
    requestAnimationFrame(renderFrame);
  };

  renderFrame();
  statusTimer = setInterval(refreshStatus, 500);
  alertsTimer = setInterval(refreshAlerts, 1000);
}

document.getElementById('join-btn').onclick = login;
document.getElementById('refresh-rooms').onclick = loadRooms;
document.getElementById('scan-btn').onclick = () => executeAction('SCAN', 'SCAN');
document.getElementById('status-btn').onclick = refreshStatus;
