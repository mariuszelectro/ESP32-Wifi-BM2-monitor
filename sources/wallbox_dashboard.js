const panel = document.createElement("section");
panel.className = "wallbox-panel";
panel.innerHTML = `
  <header class="wallbox-header">
    <div>
      <p class="wallbox-eyebrow">WALLBOX ESP</p>
      <h1>Trend napięcia</h1>
      <p class="wallbox-summary" id="history-summary">Oczekiwanie na dane</p>
      <p class="wallbox-extrema" id="extrema-summary">Min / max po 1 min: oczekiwanie na próbkę</p>
    </div>
  </header>
  <div class="chart-toolbar">
    <span>Źródło danych</span>
    <div class="source-selector" role="group" aria-label="Źródło danych wykresu">
      <button type="button" data-source="esp" aria-pressed="true">ESP32</button>
      <button type="button" data-source="bm2" aria-pressed="false">BM-2</button>
      <button type="button" data-source="temp" aria-pressed="false">Temperatura</button>
    </div>
  </div>
  <div class="chart-frame">
    <canvas id="voltage-chart" role="img" aria-label="Wykres historii napięcia ESP"></canvas>
  </div>
  <div class="devices-heading">
    <h2>Urządzenia Bluetooth</h2>
    <span id="devices-count">0 znalezionych</span>
  </div>
  <ul class="devices-list" id="devices-list" aria-live="polite">
    <li class="empty-state">Włącz skaner, aby wyszukać urządzenia.</li>
  </ul>
`;

const app = document.querySelector("esp-app");
if (app) {
  app.before(panel);
} else {
  document.body.prepend(panel);
}

const chart = panel.querySelector("#voltage-chart");
const context = chart.getContext("2d");
const summary = panel.querySelector("#history-summary");
const extremaSummary = panel.querySelector("#extrema-summary");
const devicesList = panel.querySelector("#devices-list");
const devicesCount = panel.querySelector("#devices-count");
let lastHistory = [];
let selectedSource = "esp";
let lastExtrema = {};
let refreshInProgress = false;
let controlsRefreshInProgress = false;
let previousScannerState = null;

const sourceInfo = {
  esp: { name: "ESP32", unit: "V", decimals: 3, limits: { min: 3.0, max: 4.2 } },
  bm2: { name: "BM-2", unit: "V", decimals: 3, limits: { min: 10.0, max: 15.0 } },
  temp: { name: "Temperatura MCU", unit: "°C", decimals: 2, limits: null },
};

function drawChart(values, extrema = lastExtrema, totalCount = values.length) {
  lastHistory = values;
  lastExtrema = extrema;
  const bounds = chart.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  chart.width = Math.max(1, Math.floor(bounds.width * ratio));
  chart.height = Math.max(1, Math.floor(bounds.height * ratio));
  context.setTransform(ratio, 0, 0, ratio, 0, 0);

  const width = bounds.width;
  const height = bounds.height;
  const left = 52;
  const right = width - 16;
  const top = 14;
  const bottom = height - 34;
  context.clearRect(0, 0, width, height);

  const info = sourceInfo[selectedSource];
  let limits = info.limits;
  if (!limits) {
    const lo = values.length ? Math.min(...values) : 20;
    const hi = values.length ? Math.max(...values) : 40;
    const pad = Math.max(1, (hi - lo) * 0.15);
    limits = { min: Math.floor(lo - pad), max: Math.ceil(hi + pad) };
  }
  const sourceExtrema = extrema;
  extremaSummary.textContent = sourceExtrema && Number(sourceExtrema.count) > 0
    ? `Min: ${Number(sourceExtrema.min).toFixed(info.decimals)} ${info.unit} · Max: ${Number(sourceExtrema.max).toFixed(info.decimals)} ${info.unit} · Ostatni odczyt: ${formatAge(sourceExtrema.age_s)}`
    : "Min / max / ostatni odczyt: oczekiwanie na próbkę";

  const { min, max } = limits;
  const plotWidth = right - left;
  const plotHeight = bottom - top;

  context.font = "12px system-ui, sans-serif";
  context.lineWidth = 1;
  context.strokeStyle = "#dce5e1";
  context.fillStyle = "#52645f";
  for (let line = 0; line <= 4; line++) {
    const y = top + (plotHeight * line) / 4;
    const value = max - ((max - min) * line) / 4;
    context.beginPath();
    context.moveTo(left, y);
    context.lineTo(right, y);
    context.stroke();
    context.fillText(`${value.toFixed(info.unit === "V" ? 2 : 1)} ${info.unit}`, 4, y + 4);
  }

  if (!values.length) {
    summary.textContent = "Brak próbek. Pierwsza pojawi się po odczycie ADC.";
    return;
  }

  const pointX = (index) => left + (values.length < 2 ? 0 : (plotWidth * index) / (values.length - 1));
  const pointY = (value) => bottom - ((value - min) / (max - min)) * plotHeight;
  context.beginPath();
  values.forEach((value, index) => {
    const x = pointX(index);
    const y = pointY(value);
    if (index === 0) context.moveTo(x, y);
    else context.lineTo(x, y);
  });
  context.lineWidth = 2.5;
  context.lineJoin = "round";
  context.lineCap = "round";
  context.strokeStyle = "#087f73";
  context.stroke();

  context.fillStyle = "#52645f";
  context.fillText("najstarszy", left, height - 8);
  context.textAlign = "right";
  context.fillText("najnowszy", right, height - 8);
  context.textAlign = "left";
  summary.textContent = `${info.name} · ${totalCount} próbek źródłowych · ${values.length} punktów wykresu · ostatnio ${values.at(-1).toFixed(info.decimals)} ${info.unit}`;
}

function formatAge(seconds) {
  if (!Number.isFinite(Number(seconds))) return "brak danych";
  let remaining = Math.max(0, Math.floor(Number(seconds)));
  const days = Math.floor(remaining / 86400);
  remaining %= 86400;
  const hours = Math.floor(remaining / 3600);
  remaining %= 3600;
  const minutes = Math.floor(remaining / 60);
  const secs = remaining % 60;
  return [days && `${days} d`, hours && `${hours} h`, minutes && `${minutes} min`, `${secs} s`]
    .filter(Boolean)
    .join(" ");
}

function renderDevices(deviceValues) {
  const devices = Array.isArray(deviceValues) ? deviceValues : [];
  const count = devices.length;
  devicesCount.textContent = `${count} znalezionych`;
  devicesList.replaceChildren();
  if (!count) {
    const item = document.createElement("li");
    item.className = "empty-state";
    item.textContent = "Brak znalezionych urządzeń.";
    devicesList.append(item);
    return;
  }
  for (const device of devices) {
    const item = document.createElement("li");
    const address = typeof device === "string" ? device : device.address;
    const isBm2 = address === "9C:1D:58:11:E7:86";
    item.className = isBm2 ? "bm2-device" : "";
    const name = document.createElement("strong");
    name.className = "device-name";
    name.textContent = device.name || (isBm2 ? "BM-2" : "Nazwa nieznana");
    const details = document.createElement("span");
    details.className = "device-details";
    details.textContent = `${address} · pakiety: ${Number(device.packets) || 0} · ostatnio: ${formatAge(device.last_seen_s)}`;
    item.append(name, details);
    devicesList.append(item);
  }
}

async function refreshChart() {
  if (refreshInProgress) return;
  refreshInProgress = true;
  try {
    const voltageResponse = await fetch(`/api/voltage?source=${selectedSource}`, {
      cache: "no-store",
      signal: AbortSignal.timeout(4000),
    });
    if (!voltageResponse.ok) throw new Error(`HTTP ${voltageResponse.status}`);
    const voltageData = await voltageResponse.json();
    const sample = voltageData.summary || {};
    const plot = voltageData.plot || {};
    const divisor = Number(plot.div) || 1000;
    const values = Array.isArray(plot.v)
      ? plot.v.map((raw) => Number(raw) / divisor).filter(Number.isFinite)
      : [];
    drawChart(values, sample, Number(sample.count) || 0);
  } catch (error) {
    summary.textContent = "Nie udało się pobrać wykresu z ESP32.";
    console.error("Wallbox voltage chart refresh failed", error);
  } finally {
    refreshInProgress = false;
  }
}

async function refreshDevices() {
  if (controlsRefreshInProgress) return;
  controlsRefreshInProgress = true;
  try {
    const scannerResponse = await fetch(`/switch/${encodeURIComponent("Skaner Bluetooth")}`, {
      cache: "no-store",
      signal: AbortSignal.timeout(4000),
    });
    if (!scannerResponse.ok) throw new Error(`HTTP ${scannerResponse.status}`);
    const scannerEntity = await scannerResponse.json();
    const scanning = Boolean(scannerEntity.value);
    const scanStateChanged = previousScannerState === null || scanning !== previousScannerState;
    previousScannerState = scanning;
    if (!scanning && !scanStateChanged) return;

    const devicesResponse = await fetch("/api/devices", {
      cache: "no-store",
      signal: AbortSignal.timeout(4000),
    });
    if (!devicesResponse.ok) throw new Error(`HTTP ${devicesResponse.status}`);
    const devices = await devicesResponse.json();
    renderDevices(devices.devices);
  } catch (error) {
    console.error("Wallbox Bluetooth results refresh failed", error);
  } finally {
    controlsRefreshInProgress = false;
  }
}

panel.querySelectorAll("[data-source]").forEach((button) => {
  button.addEventListener("click", () => {
    selectedSource = button.dataset.source;
    panel.querySelectorAll("[data-source]").forEach((option) => {
      option.setAttribute("aria-pressed", String(option === button));
    });
    refreshChart();
  });
});

new ResizeObserver(() => drawChart(lastHistory)).observe(chart);
refreshChart();
refreshDevices();
window.setInterval(refreshChart, 15000);
window.setInterval(refreshDevices, 5000);