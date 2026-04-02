/* N0S-RYO Dashboard — Vanilla JS, zero dependencies */
'use strict';

const API = '/api/v1';
const POLL_MS = 2000;       // Poll interval for live data
const HISTORY_POLL_MS = 10000; // Poll history less often (chart has live append)
const CHART_WINDOW = 3600;  // 1 hour of chart data (seconds)

// Color palette for per-GPU lines
const GPU_COLORS = [
  '#00d7ff', '#3fb950', '#d29922', '#f85149',
  '#a371f7', '#79c0ff', '#f778ba', '#ffa657',
  '#7ee787', '#ff7b72', '#d2a8ff', '#56d4dd',
];

// ─── State ───
let chartCtx = null;
let chartData = { timestamps: [], total: [], perGpu: [] };
let gpuCount = 0;
let prevTotal10s = null;

// ─── Helpers ───
function fmt(n) {
  if (n == null || isNaN(n)) return '—';
  if (n >= 1e6) return (n / 1e6).toFixed(2) + ' MH/s';
  if (n >= 1e3) return (n / 1e3).toFixed(1) + ' kH/s';
  return n.toFixed(1) + ' H/s';
}

function fmtInt(n) {
  if (n == null || isNaN(n)) return '—';
  return Number(n).toLocaleString();
}

function fmtTime(sec) {
  if (sec < 60) return sec + 's';
  if (sec < 3600) return Math.floor(sec / 60) + 'm ' + (sec % 60) + 's';
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  return h + 'h ' + m + 'm';
}

async function api(path) {
  try {
    const r = await fetch(API + path);
    if (!r.ok) return null;
    return await r.json();
  } catch { return null; }
}

// ─── DOM refs ───
const $  = id => document.getElementById(id);
const el = {
  poolAddr:   $('pool-addr'),
  connDot:    $('conn-dot'),
  version:    $('version-str'),
  uptime:     $('uptime-str'),
  totalHs:    $('total-hs'),
  totalHsSub: $('total-hs-sub'),
  shares:     $('shares-val'),
  sharesSub:  $('shares-sub'),
  diff:       $('diff-val'),
  diffSub:    $('diff-sub'),
  ping:       $('ping-val'),
  pingSub:    $('ping-sub'),
  gpuTbody:   $('gpu-tbody'),
  piAddr:     $('pi-addr'),
  piHashes:   $('pi-hashes'),
  piAvgShare: $('pi-avg-share'),
  piUptime:   $('pi-uptime'),
  topDiffs:   $('top-diffs'),
  chartLegend:$('chart-legend'),
};

// ─── Update Functions ───

async function updateStatus() {
  const d = await api('/status');
  if (!d) return;
  el.connDot.className = 'conn-dot ' + (d.connected ? 'connected' : 'disconnected');
  el.poolAddr.textContent = d.pool || '—';
  el.uptime.textContent = fmtTime(d.uptime_seconds || 0);
}

async function updateVersion() {
  const d = await api('/version');
  if (!d) return;
  el.version.textContent = d.version_short || d.version || '—';
}

async function updateHashrate() {
  const d = await api('/hashrate');
  if (!d) return;

  const t = d.total || {};
  const h10 = t.hashrate_10s || 0;
  el.totalHs.textContent = fmt(h10);

  // Trend arrow
  if (prevTotal10s != null) {
    const pct = prevTotal10s > 0 ? ((h10 - prevTotal10s) / prevTotal10s * 100) : 0;
    const arrow = pct > 0.5 ? '▲' : pct < -0.5 ? '▼' : '—';
    const color = pct > 0.5 ? 'var(--green)' : pct < -0.5 ? 'var(--red)' : 'var(--text-dim)';
    el.totalHsSub.innerHTML = `<span style="color:${color}">${arrow} ${Math.abs(pct).toFixed(1)}%</span> &bull; peak ${fmt(d.highest || 0)}`;
  } else {
    el.totalHsSub.innerHTML = `peak ${fmt(d.highest || 0)}`;
  }
  prevTotal10s = h10;

  // Append to chart data from live poll
  const now = Date.now();
  chartData.timestamps.push(now);
  chartData.total.push(h10);

  // Per-GPU from threads
  const threads = d.threads || [];
  gpuCount = Math.max(gpuCount, threads.length);
  while (chartData.perGpu.length < gpuCount) chartData.perGpu.push([]);
  for (let i = 0; i < gpuCount; i++) {
    chartData.perGpu[i].push(threads[i] ? (threads[i].hashrate_10s || 0) : 0);
  }

  // Trim to window
  const cutoff = now - CHART_WINDOW * 1000;
  while (chartData.timestamps.length > 0 && chartData.timestamps[0] < cutoff) {
    chartData.timestamps.shift();
    chartData.total.shift();
    for (let i = 0; i < chartData.perGpu.length; i++) chartData.perGpu[i].shift();
  }

  drawChart();
}

async function updatePool() {
  const d = await api('/pool');
  if (!d) return;

  const acc = d.shares_accepted || 0;
  const rej = d.shares_rejected || 0;
  el.shares.textContent = fmtInt(acc) + ' / ' + fmtInt(rej);
  el.sharesSub.textContent = 'accepted / rejected';
  if (rej > 0) el.shares.style.color = 'var(--yellow)';
  else el.shares.style.color = '';

  el.diff.textContent = fmtInt(d.difficulty);
  el.ping.textContent = d.ping_ms ? d.ping_ms + ' ms' : '—';

  el.piAddr.textContent = d.address || '—';
  el.piHashes.textContent = fmtInt(d.hashes_total);
  el.piAvgShare.textContent = d.avg_share_time ? d.avg_share_time.toFixed(1) + 's' : '—';
  el.piUptime.textContent = fmtTime(d.uptime_seconds || 0);

  // Top difficulties
  const diffs = (d.top_difficulties || []).filter(x => x > 0);
  el.topDiffs.innerHTML = diffs.map(x =>
    `<span class="diff-badge">${fmtInt(x)}</span>`
  ).join('');
}

async function updateGpus() {
  const d = await api('/gpus');
  if (!d) return;

  const gpus = d.gpus || [];
  let html = '';
  for (const g of gpus) {
    const t = g.telemetry || {};
    html += `<tr>
      <td>${g.index}</td>
      <td>${g.backend || '—'}</td>
      <td class="num">${fmtInt(Math.round(g.hashrate || 0))}</td>
      <td class="num">${t.temp_c != null ? t.temp_c + '°C' : '—'}</td>
      <td class="num">${t.power_w != null ? t.power_w + 'W' : '—'}</td>
      <td class="num">${t.fan_pct != null ? t.fan_pct + '%' : (t.fan_rpm != null ? t.fan_rpm + ' RPM' : '—')}</td>
      <td class="num">${t.gpu_clock_mhz != null ? t.gpu_clock_mhz + ' MHz' : '—'}</td>
      <td class="num">${t.mem_clock_mhz != null ? t.mem_clock_mhz + ' MHz' : '—'}</td>
    </tr>`;
  }
  el.gpuTbody.innerHTML = html;
}

async function loadHistory() {
  const d = await api('/hashrate/history');
  if (!d || !d.samples || d.samples.length === 0) return;

  // Replace chart data with history
  chartData.timestamps = d.samples.map(s => s.t);
  chartData.total = d.samples.map(s => s.total);
  gpuCount = d.gpu_count || 0;
  chartData.perGpu = [];
  for (let g = 0; g < gpuCount; g++) {
    chartData.perGpu.push(d.samples.map(s => (s.gpus && s.gpus[g]) || 0));
  }
  drawChart();
}

// ─── Chart Drawing (pure Canvas, no deps) ───

function initChart() {
  const canvas = $('hashrate-chart');
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  chartCtx = canvas.getContext('2d');
  chartCtx.scale(dpr, dpr);
}

function drawChart() {
  if (!chartCtx) return;
  const canvas = chartCtx.canvas;
  const dpr = window.devicePixelRatio || 1;
  const W = canvas.width / dpr;
  const H = canvas.height / dpr;

  const pad = { top: 10, right: 12, bottom: 24, left: 60 };
  const cW = W - pad.left - pad.right;
  const cH = H - pad.top - pad.bottom;

  chartCtx.clearRect(0, 0, W, H);

  const n = chartData.timestamps.length;
  if (n < 2) return;

  // Y range from total hashrate
  let yMin = Infinity, yMax = -Infinity;
  for (let i = 0; i < n; i++) {
    const v = chartData.total[i];
    if (v < yMin) yMin = v;
    if (v > yMax) yMax = v;
  }
  // Add 10% padding
  const yRange = yMax - yMin || 1;
  yMin = Math.max(0, yMin - yRange * 0.1);
  yMax = yMax + yRange * 0.1;

  const tMin = chartData.timestamps[0];
  const tMax = chartData.timestamps[n - 1];
  const tRange = tMax - tMin || 1;

  function xPos(t) { return pad.left + ((t - tMin) / tRange) * cW; }
  function yPos(v) { return pad.top + cH - ((v - yMin) / (yMax - yMin)) * cH; }

  // Grid lines
  chartCtx.strokeStyle = 'rgba(48, 54, 61, 0.6)';
  chartCtx.lineWidth = 0.5;
  const ySteps = 4;
  for (let i = 0; i <= ySteps; i++) {
    const y = pad.top + (cH / ySteps) * i;
    chartCtx.beginPath();
    chartCtx.moveTo(pad.left, y);
    chartCtx.lineTo(W - pad.right, y);
    chartCtx.stroke();

    // Y-axis labels
    const val = yMax - (i / ySteps) * (yMax - yMin);
    chartCtx.fillStyle = '#8b949e';
    chartCtx.font = '10px ' + getComputedStyle(document.body).getPropertyValue('--mono');
    chartCtx.textAlign = 'right';
    chartCtx.fillText(fmt(val).replace(' H/s',''), pad.left - 6, y + 3);
  }

  // X-axis time labels
  chartCtx.textAlign = 'center';
  const xSteps = Math.min(6, Math.floor(cW / 80));
  for (let i = 0; i <= xSteps; i++) {
    const t = tMin + (tRange / xSteps) * i;
    const x = xPos(t);
    const d = new Date(t);
    const label = d.getHours().toString().padStart(2,'0') + ':' + d.getMinutes().toString().padStart(2,'0');
    chartCtx.fillStyle = '#8b949e';
    chartCtx.fillText(label, x, H - 4);
  }

  // Helper: draw a line series
  function drawLine(data, color, width) {
    chartCtx.beginPath();
    chartCtx.strokeStyle = color;
    chartCtx.lineWidth = width;
    chartCtx.lineJoin = 'round';
    let first = true;
    for (let i = 0; i < n; i++) {
      const x = xPos(chartData.timestamps[i]);
      const y = yPos(data[i]);
      if (first) { chartCtx.moveTo(x, y); first = false; }
      else chartCtx.lineTo(x, y);
    }
    chartCtx.stroke();
  }

  // Per-GPU lines (thin, behind total)
  for (let g = 0; g < chartData.perGpu.length; g++) {
    drawLine(chartData.perGpu[g], GPU_COLORS[g % GPU_COLORS.length] + '80', 1);
  }

  // Total line (thick, on top)
  drawLine(chartData.total, '#00d7ff', 2);

  // Glow effect under total line
  chartCtx.beginPath();
  for (let i = 0; i < n; i++) {
    const x = xPos(chartData.timestamps[i]);
    const y = yPos(chartData.total[i]);
    if (i === 0) chartCtx.moveTo(x, y);
    else chartCtx.lineTo(x, y);
  }
  chartCtx.lineTo(xPos(tMax), pad.top + cH);
  chartCtx.lineTo(xPos(tMin), pad.top + cH);
  chartCtx.closePath();
  const grad = chartCtx.createLinearGradient(0, pad.top, 0, pad.top + cH);
  grad.addColorStop(0, 'rgba(0, 215, 255, 0.15)');
  grad.addColorStop(1, 'rgba(0, 215, 255, 0.0)');
  chartCtx.fillStyle = grad;
  chartCtx.fill();

  // Legend
  let legend = '<span style="color:#00d7ff">● Total</span>';
  for (let g = 0; g < Math.min(gpuCount, 8); g++) {
    legend += ` <span style="color:${GPU_COLORS[g % GPU_COLORS.length]}">● GPU ${g}</span>`;
  }
  el.chartLegend.innerHTML = legend;
}

// ─── Init & Poll Loop ───

async function init() {
  initChart();
  window.addEventListener('resize', () => { initChart(); drawChart(); });

  // Initial data load
  await Promise.all([updateVersion(), loadHistory()]);

  // Main poll loop
  async function tick() {
    await Promise.all([updateStatus(), updateHashrate(), updatePool(), updateGpus()]);
    setTimeout(tick, POLL_MS);
  }
  tick();

  // History refresh (less frequent)
  setInterval(loadHistory, HISTORY_POLL_MS);
}

init();
