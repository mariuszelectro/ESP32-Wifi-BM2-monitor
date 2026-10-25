const panel = document.createElement("section");
panel.className = "wallbox-panel";
panel.innerHTML = `
  <nav class="dashboard-tabs" role="tablist" aria-label="Panel Wallbox">
    <button type="button" id="tab-view" role="tab" data-tab="view" aria-controls="page-view" aria-selected="true">Widok</button>
    <button type="button" id="tab-settings" role="tab" data-tab="settings" aria-controls="page-settings" aria-selected="false" tabindex="-1">Ustawienia</button>
  </nav>
  <section id="page-view" role="tabpanel" aria-labelledby="tab-view">
  <header class="wallbox-header">
    <div>
      <p class="wallbox-eyebrow">WALLBOX ESP</p>
      <h1>Trend napięcia</h1>
      <p class="wallbox-summary" id="history-summary">Oczekiwanie na dane</p>
      <p class="wallbox-extrema" id="extrema-summary">Min / max po 1 min: oczekiwanie na próbkę</p>
      <p class="wallbox-extrema" id="bm2-statistics">BM-2: oczekiwanie na statystykę odczytów</p>
    </div>
    <button type="button" id="debug-toggle" aria-pressed="false">Pokaż log debug</button>
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
  </section>
  <section id="page-settings" role="tabpanel" aria-labelledby="tab-settings" hidden>
    <h1>Ustawienia</h1>
    <h2>Powiadomienia ntfy</h2>
    <p>Alarm przy każdym poprawnym pomiarze BM2: powyżej 5 V i poniżej 11,9 V. Zmierzone napięcie jest dopisywane automatycznie.</p>
    <form id="ntfy-form" class="ntfy-form">
      <label>Serwer ntfy (HTTPS)
        <input id="ntfy-server" name="server" type="url" required maxlength="160" autocomplete="off" value="https://ntfy.sh">
      </label>
      <label>Topic
        <input id="ntfy-topic" name="topic" required maxlength="64" pattern="[A-Za-z0-9_\\-]+" autocomplete="off" value="RZ500eBM2acu12VnotifyForMe">
      </label>
      <label>Token (opcjonalny)
        <input id="ntfy-token" name="token" type="password" maxlength="128" pattern="[A-Za-z0-9_\\-]*" autocomplete="new-password">
      </label>
      <label>Treść powiadomienia
        <textarea id="ntfy-message" name="message" required maxlength="240" rows="3">Akumulator wymaga doladowania.</textarea>
      </label>
      <p>Domyślny serwer: https://ntfy.sh. Możesz podać własny serwer HTTPS, opcjonalnie z portem (bez ścieżki). Token może pozostać pusty. Ustawienia są zapamiętywane w urządzeniu.</p>
      <div class="ntfy-actions">
        <button type="submit" id="ntfy-save" disabled>Zapisz ustawienia</button>
        <button type="button" id="ntfy-test" disabled>Test message</button>
      </div>
    </form>
    <p id="ntfy-feedback" role="status" aria-live="polite">Oczekiwanie na odczyt ustawień.</p>
    <p id="ntfy-delivery" role="status" aria-live="polite"></p>
  </section>
  <p id="native-panel-status" role="status" aria-live="polite" hidden></p>
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
const bm2Statistics = panel.querySelector("#bm2-statistics");
const devicesList = panel.querySelector("#devices-list");
const devicesCount = panel.querySelector("#devices-count");
let lastHistory = [];
let selectedSource = "esp";
let lastExtrema = {};
let refreshInProgress = false;
let controlsRefreshInProgress = false;
let previousScannerState = null;
let selectedTab = "view";
let debugVisible = false;
let ntfyLoaded = false;
let ntfyBusy = false;

const ntfyFields = {
  server: panel.querySelector("#ntfy-server"),
  topic: panel.querySelector("#ntfy-topic"),
  token: panel.querySelector("#ntfy-token"),
  message: panel.querySelector("#ntfy-message"),
};
const ntfyFeedback = panel.querySelector("#ntfy-feedback");
const ntfyDelivery = panel.querySelector("#ntfy-delivery");
const ntfySave = panel.querySelector("#ntfy-save");
const ntfyTest = panel.querySelector("#ntfy-test");
const debugToggle = panel.querySelector("#debug-toggle");

function setDebugVisible(visible) {
  debugVisible = visible;
  if (app) app.dataset.wallboxDebug = String(visible);
  debugToggle.setAttribute("aria-pressed", String(visible));
  debugToggle.textContent = visible ? "Ukryj log debug" : "Pokaż log debug";
}

const settingsGroups = new Set(["System i konserwacja", "Zasilanie i kalibracja"]);
let nativeTable = null;

function filterNativeGroups(root, tab) {
  root.querySelectorAll(".tab-header").forEach((heading) => {
    const container = heading.nextElementSibling;
    if (!container?.classList.contains("tab-container")) return;
    const page = settingsGroups.has(heading.textContent.trim()) ? "settings" : "view";
    heading.dataset.wallboxPage = page;
    container.dataset.wallboxPage = page;
  });
  nativeTable.dataset.wallboxTab = tab;
}

function addNativeStyle(root, css) {
  const style = document.createElement("style");
  style.textContent = css;
  root.append(style);
}

async function initializeNativeTabs() {
  if (!app) return;
  try {
    await customElements.whenDefined("esp-app");
    await app.updateComplete;
    if (!app.shadowRoot) throw new Error("Brak struktury panelu esp-app");
    addNativeStyle(app.shadowRoot, `
      :host([data-wallbox-tab="view"]) #col_entities > .tab-header,
      :host([data-wallbox-tab="view"]) #col_entities > form,
      :host([data-wallbox-tab="settings"]) header,
      :host([data-wallbox-tab="settings"]) #col_logs,
      :host(:not([data-wallbox-debug="true"])) #col_logs { display: none !important; }
      :host([data-wallbox-tab="settings"]) main,
      :host(:not([data-wallbox-debug="true"])) main { display: block; }
      :host([data-wallbox-tab="settings"]) #col_entities,
      :host(:not([data-wallbox-debug="true"])) #col_entities { width: 100%; }
    `);
    const connectTable = async () => {
      const table = app.shadowRoot.querySelector("esp-entity-table");
      if (!table || table === nativeTable) return;
      await customElements.whenDefined("esp-entity-table");
      await table.updateComplete;
      if (table === nativeTable) return;
      if (!table.shadowRoot) throw new Error("Brak struktury tabeli ESPHome");
      nativeTable = table;
      addNativeStyle(table.shadowRoot, `
        :host([data-wallbox-tab="view"]) [data-wallbox-page="settings"],
        :host([data-wallbox-tab="settings"]) [data-wallbox-page="view"],
        :host([data-wallbox-tab="settings"]) .singlebutton-row { display: none !important; }
      `);
      const refresh = () => filterNativeGroups(table.shadowRoot, selectedTab);
      new MutationObserver(refresh).observe(table.shadowRoot, { childList: true, subtree: true, characterData: true });
      refresh();
    };
    const reportError = (error) => {
      const status = panel.querySelector("#native-panel-status");
      status.hidden = false;
      status.textContent = `Nie udało się podzielić panelu ESPHome na zakładki: ${error.message}`;
      console.error("Wallbox native tab setup failed", error);
    };
    new MutationObserver(() => { connectTable().catch(reportError); })
      .observe(app.shadowRoot, { childList: true, subtree: true });
    await connectTable();
    app.dataset.wallboxTab = selectedTab;
  } catch (error) {
    const status = panel.querySelector("#native-panel-status");
    status.hidden = false;
    status.textContent = `Nie udało się przygotować zakładek ESPHome: ${error.message}`;
    console.error("Wallbox native tab setup failed", error);
  }
}

function selectTab(tab) {
  selectedTab = tab;
  document.body.dataset.wallboxTab = tab;
  if (app) app.dataset.wallboxTab = tab;
  if (nativeTable) filterNativeGroups(nativeTable.shadowRoot, tab);
  panel.querySelector("#page-view").hidden = tab !== "view";
  panel.querySelector("#page-settings").hidden = tab !== "settings";
  panel.querySelectorAll("[data-tab]").forEach((button) => {
    const active = button.dataset.tab === tab;
    button.setAttribute("aria-selected", String(active));
    button.tabIndex = active ? 0 : -1;
  });
  if (tab === "settings" && !ntfyLoaded && !ntfyBusy) loadNtfySettings();
  if (tab === "view") drawChart(lastHistory);
}

async function ntfyRequest(domain, name, action = "", value) {
  const options = { cache: "no-store", signal: AbortSignal.timeout(5000) };
  if (action) {
    options.method = "POST";
    if (value !== undefined) {
      options.headers = { "Content-Type": "application/x-www-form-urlencoded" };
      options.body = new URLSearchParams({ value }).toString();
    }
  }
  const response = await fetch(`/${domain}/${encodeURIComponent(name)}${action ? `/${action}` : ""}`, options);
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return action ? null : response.json();
}

function setNtfyBusy(busy) {
  ntfyBusy = busy;
  ntfySave.disabled = busy || !ntfyLoaded;
  ntfyTest.disabled = busy || !ntfyLoaded;
}

async function loadNtfySettings() {
  setNtfyBusy(true);
  try {
    const values = await Promise.all(Object.keys(ntfyFields).map((key) => ntfyRequest("text", `NTFY ${key}`)));
    Object.keys(ntfyFields).forEach((key, index) => { ntfyFields[key].value = values[index].value; });
    ntfyLoaded = true;
    ntfyFeedback.textContent = "Ustawienia odczytane.";
    await refreshNtfyStatus();
  } catch (error) {
    ntfyFeedback.textContent = `Nie udało się odczytać ustawień: ${error.message}. Wróć do zakładki, aby ponowić.`;
    console.error("Wallbox ntfy settings load failed", error);
  } finally {
    setNtfyBusy(false);
  }
}

async function refreshNtfyStatus() {
  try {
    const state = await ntfyRequest("text_sensor", "NTFY status");
    ntfyDelivery.textContent = typeof state.value === "string" ? state.value : "Brak wysłanych wiadomości.";
  } catch (error) {
    ntfyDelivery.textContent = `Nie udało się odczytać statusu ntfy: ${error.message}`;
    console.error("Wallbox ntfy status refresh failed", error);
  }
}

async function saveNtfySettings(sendTest = false) {
  if (ntfyBusy || !ntfyLoaded || !panel.querySelector("#ntfy-form").reportValidity()) return;
  const values = Object.fromEntries(Object.entries(ntfyFields).map(([key, field]) => [key, field.value]));
  const serverMatch = /^https:\/\/([A-Za-z0-9.-]+)(?::([0-9]{1,5}))?\/?$/.exec(values.server);
  if (values.server.length > 160 || !serverMatch || serverMatch[1].startsWith(".") ||
      serverMatch[1].endsWith(".") || serverMatch[1].includes("..") ||
      (serverMatch[2] !== undefined && (Number(serverMatch[2]) < 1 || Number(serverMatch[2]) > 65535))) {
    ntfyFeedback.textContent = "Niepoprawny serwer. Podaj https://nazwa-serwera, opcjonalnie z portem 1–65535, bez ścieżki.";
    return;
  }
  if (!/^[A-Za-z0-9_-]{1,64}$/.test(values.topic) || !/^[A-Za-z0-9_-]{0,128}$/.test(values.token) ||
      !values.message.trim() || new TextEncoder().encode(values.message).length > 240) {
    ntfyFeedback.textContent = "Niepoprawne ustawienia. Topic i token: litery, cyfry, _ i -. Treść: 1–240 bajtów UTF-8.";
    return;
  }
  setNtfyBusy(true);
  try {
    await Promise.all(Object.entries(values).map(async ([key, value]) => {
      await ntfyRequest("text", `NTFY ${key}`, "set", value);
      // ESPHome acknowledges the request before the deferred entity update.
      for (let attempt = 0; attempt < 10; attempt++) {
        const saved = await ntfyRequest("text", `NTFY ${key}`);
        if (saved.value === value) return;
        await new Promise((resolve) => window.setTimeout(resolve, 100));
      }
      throw new Error(`Urządzenie nie potwierdziło zapisu pola ${key}`);
    }));
    if (sendTest) {
      await ntfyRequest("button", "NTFY test", "press");
      ntfyFeedback.textContent = "Zlecono test. Wynik wysyłki pojawi się poniżej.";
      ntfyDelivery.textContent = "Oczekiwanie na wynik testu...";
    } else {
      ntfyFeedback.textContent = "Ustawienia zapisane.";
    }
  } catch (error) {
    ntfyFeedback.textContent = `Błąd zapisu lub zlecenia testu: ${error.message}. Sprawdź ustawienia przed ponowieniem.`;
    console.error("Wallbox ntfy settings save failed", error);
  } finally {
    setNtfyBusy(false);
  }
}

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
  const minLabel = sourceExtrema.min == null
    ? "brak danych"
    : `${Number(sourceExtrema.min).toFixed(info.decimals)} ${info.unit}`;
  extremaSummary.textContent = sourceExtrema && Number(sourceExtrema.count) > 0
    ? `Min: ${minLabel} · Max: ${Number(sourceExtrema.max).toFixed(info.decimals)} ${info.unit} · Ostatni odczyt: ${formatAge(sourceExtrema.age_s)}`
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
    const rssi = Number.isFinite(device.rssi) ? `${device.rssi} dBm` : "brak danych";
    details.textContent = `${address} · RSSI: ${rssi} · pakiety: ${Number(device.packets) || 0} · ostatnio: ${formatAge(device.last_seen_s)}`;
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
    const stats = voltageData.bm2_stats;
    if (stats) {
      const percent = stats.attempts > 0 ? ((stats.successes / stats.attempts) * 100).toFixed(1) : "0.0";
      bm2Statistics.textContent = `BM-2: ${stats.successes} / ${stats.attempts} udanych odczytów (${percent}%) · pamięć RTC`;
    }
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

panel.querySelectorAll("[data-tab]").forEach((button) => {
  button.addEventListener("click", () => selectTab(button.dataset.tab));
  button.addEventListener("keydown", (event) => {
    if (!["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) return;
    event.preventDefault();
    const tab = event.key === "Home" ? "view" : event.key === "End" ? "settings"
      : selectedTab === "view" ? "settings" : "view";
    selectTab(tab);
    panel.querySelector(`#tab-${tab}`).focus();
  });
});
panel.querySelector("#ntfy-form").addEventListener("submit", (event) => {
  event.preventDefault();
  saveNtfySettings();
});
ntfyTest.addEventListener("click", () => saveNtfySettings(true));
debugToggle.addEventListener("click", () => setDebugVisible(!debugVisible));
setDebugVisible(false);
document.body.dataset.wallboxTab = "view";
initializeNativeTabs();

new ResizeObserver(() => drawChart(lastHistory)).observe(chart);
refreshChart();
refreshDevices();
window.setInterval(refreshChart, 15000);
window.setInterval(refreshDevices, 5000);
window.setInterval(() => {
  if (selectedTab === "settings" && ntfyLoaded && !ntfyBusy) refreshNtfyStatus();
}, 5000);