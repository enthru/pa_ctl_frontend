#include "web.h"
#include "uart_handler.h"   // sendStateData, sendSettingsData, sendCalibrationData

// ─── Shared alert JS snippet (included in every page) ────────────────────────
// Stored as a PROGMEM-friendly raw literal and concatenated at build time.
// Each page appends ALERT_JS + ALERT_HTML inside its own <body>.

static const char ALERT_JS[] PROGMEM = R"js(
    function closeAlert() {
        document.getElementById('alertOverlay').classList.remove('alert-visible');
        document.body.style.overflow = 'auto';
        fetch('/resetalert', { method: 'POST' }).catch(console.error);
    }
    function closeError() {
        document.body.style.overflow = 'auto';
        document.getElementById('alertOverlay').classList.remove('alert-visible');
        fetch('/reseterror', { method: 'POST' }).catch(console.error);
    }
    function showAlert(message, alertType = 'alert') {
        document.getElementById('alertMessage').textContent = message;
        const alertModal  = document.querySelector('.alert-modal');
        const alertTitle  = document.querySelector('.alert-title');
        const alertButton = document.querySelector('.alert-button');
        const overlay     = document.getElementById('alertOverlay');
        if (alertType === 'error') {
            alertModal.style.borderColor  = '#FF4444';
            alertTitle.textContent        = 'ERROR';
            alertTitle.style.color        = '#FF4444';
            alertButton.textContent       = 'Close Error';
            alertButton.style.backgroundColor = '#FF4444';
            alertButton.onclick = closeError;
        } else {
            alertModal.style.borderColor  = '#FFA500';
            alertTitle.textContent        = 'ALERT';
            alertTitle.style.color        = '#FFA500';
            alertButton.textContent       = 'Reset Alert';
            alertButton.style.backgroundColor = '#FFA500';
            alertButton.onclick = closeAlert;
        }
        overlay.classList.add('alert-visible');
        document.body.style.overflow = 'hidden';
    }
    function hideAlert() {
        document.getElementById('alertOverlay').classList.remove('alert-visible');
        document.body.style.overflow = 'auto';
    }
    function alertText(reason) {
        const map = {
            'water_temp': 'Water temperature too high',
            'plate_temp': 'Waterblock temperature too high',
            'coeff':      'Low efficiency',
            'swr':        'High SWR',
            'voltage':    'Overvoltage',
            'current':    'Current too high',
            'ipower':     'High input power',
            'band':       'Band error'
        };
        return map[reason] || 'Unknown error';
    }
    function pollEvents() {
        fetch('/events')
        .then(r => r.json())
        .then(data => {
            if (data.has_error) { showAlert("Communication Error: " + (data.error_message || ""), 'error'); return; }
            if (data.alarm_active) showAlert(alertText(data.alert_reason), 'alert');
            else hideAlert();
        })
        .catch(err => console.error('Events error:', err));
    }
    document.addEventListener('DOMContentLoaded', () => {
        pollEvents();
        setInterval(pollEvents, 500);
        document.getElementById('alertOverlay').addEventListener('click', function(e) {
            if (e.target === this) e.preventDefault();
        });
    });
)js";

static const char ALERT_HTML[] PROGMEM = R"html(
    <div id="alertOverlay" class="alert-overlay">
        <div class="alert-modal">
            <div class="alert-title">ALERT</div>
            <div id="alertMessage" class="alert-message"></div>
            <button class="alert-button" onclick="closeAlert()">Reset Alert</button>
        </div>
    </div>
)html";

// ─── Helper: page wrapper ─────────────────────────────────────────────────────
static String pageHead(const char* title) {
    String h = "<!DOCTYPE html><html><head><title>";
    h += title;
    h += R"(</title><meta name="viewport" content="width=device-width, initial-scale=1">)";
    h += R"(<link rel="stylesheet" href="/style.css"></head><body>)";
    return h;
}

// ─── /events ─────────────────────────────────────────────────────────────────

void handleEvents() {
    String r = "{";
    r += "\"alarm_active\":"  + String(status.alarm    ? "true" : "false") + ",";
    r += "\"alert_reason\":\"" + String(status.alert_reason) + "\",";
    r += "\"has_error\":"     + String(hasWebError     ? "true" : "false") + ",";
    r += "\"error_message\":\"" + String(webErrorMessage) + "\"";
    r += "}";
    server.send(200, "application/json", r);
    if (hasWebError) { hasWebError = false; webErrorMessage = ""; }
}

// ─── /resetalert, /reseterror ─────────────────────────────────────────────────

void handleResetAlert() {
    state.alarm  = false; status.alarm = false;
    status.ptt   = false; state.ptt    = false;
    state.state  = false; status.state = false;
    sendStateData();
    unsigned long start = millis();
    while (millis() - start < 1000) {
        lv_timer_handler();
        delay(5);
    }
    server.send(200, "text/plain", "OK");
}

void handleResetError() {
    hasWebError     = false;
    webErrorMessage = "";
    server.send(200, "text/plain", "OK");
}

// ─── / (main status page) ─────────────────────────────────────────────────────

void handleRoot() {
    String html = pageHead("Status");
    html += R"rawliteral(
    <div class="container" id="statusOutGrid">
        <h1>Amplifier Status</h1>
        <div class="status-grid" id="statusGrid">
            <div class="status-item"><label>Power:</label><span id="pwr">0W</span>
                <div class="bar"><div id="pwrBar" class="bar-fill"></div></div></div>
            <div class="status-item"><label>SWR:</label><span id="swr">0</span></div>
            <div class="status-item"><label>Reflected:</label><span id="ref">0W</span></div>
            <div class="status-item"><label>Voltage:</label><span id="vol">0V</span></div>
            <div class="status-item"><label>Current:</label><span id="cur">0A</span></div>
            <div class="status-item"><label>Water Temp:</label><span id="waterTmp">0C</span></div>
            <div class="status-item"><label>Plate Temp:</label><span id="plateTmp">0C</span></div>
            <div class="status-item"><label>State:</label>
                <div class="toggle-switch">
                    <input type="checkbox" id="stateToggle" class="toggle-input">
                    <label for="stateToggle" class="toggle-label"></label>
                </div>
            </div>
            <div class="status-item"><label>PWM fan:</label><span id="pwm_cooler">-</span></div>
            <div class="status-item"><label>PWM pump:</label><span id="pwm_pump">-</span></div>
            <div class="status-item"><label>Coeff.:</label><span id="coeff">-</span></div>
            <div class="status-item"><label>Band:</label><span id="band">-</span></div>
            <div class="status-item"><label>PTT:</label><span id="ptt">OFF</span></div>
        </div>
        <div class="navigation">
            <button onclick="location.href='/band'">Band Selection</button>
            <button onclick="location.href='/settings'">Settings</button>
            <button onclick="location.href='/calibration'">Calibration</button>
        </div>
    </div>
    <script>
    )rawliteral";
    html += ALERT_JS;
    html += R"rawliteral(
        function updateStatus() {
            fetch('/status').then(r => r.json()).then(data => {
                const pv = data.fwd || 0;
                document.getElementById('pwr').textContent  = pv + 'W';
                document.getElementById('pwrBar').style.width = Math.min(pv / 12, 100) + '%';
                document.getElementById('swr').textContent  = data.swr || 0;
                document.getElementById('ref').textContent  = (data.ref || 0) + 'W';
                document.getElementById('vol').textContent  = (data.voltage || 0).toFixed(1) + 'V';
                document.getElementById('cur').textContent  = (data.current || 0).toFixed(1) + 'A';
                document.getElementById('waterTmp').textContent = (data.water_temp || 0).toFixed(1) + 'C';
                document.getElementById('plateTmp').textContent = (data.plate_temp || 0).toFixed(1) + 'C';
                document.getElementById('coeff').textContent    = (data.coeff || 0).toFixed(1) + '%';
                document.getElementById('band').textContent     = data.band || '-';
                document.getElementById('ptt').textContent      = data.ptt ? 'ON' : 'OFF';
                document.getElementById('pwm_pump').textContent   = (data.pwm_pump   || 0) + '%';
                document.getElementById('pwm_cooler').textContent = (data.pwm_cooler || 0) + '%';
                document.getElementById('stateToggle').checked = data.state || false;
                const pttOn = data.ptt;
                document.getElementById('pwrBar').style.backgroundColor = pttOn ? '#FF0000' : '#007AFF';
                document.body.classList.toggle('ptt-on', pttOn);
                document.getElementById('statusGrid').classList.toggle('ptt-on', pttOn);
                document.getElementById('statusOutGrid').classList.toggle('ptt-on', pttOn);
            });
        }
        document.getElementById('stateToggle').addEventListener('change', function() {
            fetch('/setstate', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
                body:'state=' + this.checked }).catch(console.error);
        });
        setInterval(updateStatus, 500);
        updateStatus();
    </script>
    )rawliteral";
    html += ALERT_HTML;
    html += "</body></html>";
    server.send(200, "text/html", html);
}

// ─── /status (JSON) ───────────────────────────────────────────────────────────

void handleStatus() {
    if (waitingForResponse != RESPONSE_NONE) {
        server.send(503, "application/json", "{\"error\":\"Busy\"}");
        return;
    }
    String r = "{";
    r += "\"fwd\":"        + String(status.fwd)          + ",";
    r += "\"ref\":"        + String(status.ref)          + ",";
    r += "\"swr\":"        + String(status.swr)          + ",";
    r += "\"voltage\":"    + String(status.voltage,   1) + ",";
    r += "\"current\":"    + String(status.current,   1) + ",";
    r += "\"water_temp\":" + String(status.water_temp,1) + ",";
    r += "\"plate_temp\":" + String(status.plate_temp,1) + ",";
    r += "\"coeff\":"      + String(status.coeff,     1) + ",";
    r += "\"ptt\":"        + String(status.ptt    ? "true":"false") + ",";
    r += "\"state\":"      + String(status.state   ? "true":"false") + ",";
    r += "\"alarm\":"      + String(status.alarm   ? "true":"false") + ",";
    r += "\"pwm_pump\":"   + String(status.pwm_pump)     + ",";
    r += "\"pwm_cooler\":" + String(status.pwm_cooler)   + ",";
    r += "\"alert_reason\":\"" + String(status.alert_reason) + "\",";
    r += "\"band\":\""     + String(status.band) + "\"";
    r += "}";
    server.send(200, "application/json", r);
}

// ─── /band ────────────────────────────────────────────────────────────────────

void handleBandPage() {
    String html = pageHead("Band Selection");
    html += R"rawliteral(
    <div class="container">
        <h1>Band Selection</h1>
        <div class="band-grid">
            <button class="band-btn" onclick="setBand('160m')">160m</button>
            <button class="band-btn" onclick="setBand('80m')">80m</button>
            <button class="band-btn" onclick="setBand('60m')">60m</button>
            <button class="band-btn" onclick="setBand('40m')">40m</button>
            <button class="band-btn" onclick="setBand('30m')">30m</button>
            <button class="band-btn" onclick="setBand('20m')">20m</button>
            <button class="band-btn" onclick="setBand('17m')">17m</button>
            <button class="band-btn" onclick="setBand('15m')">15m</button>
            <button class="band-btn" onclick="setBand('12m')">12m</button>
            <button class="band-btn" onclick="setBand('10m')">10m</button>
            <button class="band-btn" onclick="setBand('6m')">6m</button>
        </div>
        <div class="navigation">
            <button onclick="location.href='/'">Back to Status</button>
            <span id="message" class="message"></span>
        </div>
    </div>
    <script>
    )rawliteral";
    html += ALERT_JS;
    html += R"rawliteral(
        function setBand(band) {
            fetch('/setband', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'band='+band })
            .then(() => { document.getElementById('message').textContent = 'Band set to: ' + band;
                setTimeout(() => window.location.href='/', 50); })
            .catch(() => { document.getElementById('message').textContent = 'Error setting band'; });
        }
        document.addEventListener('DOMContentLoaded', () => {
            fetch('/status').then(r => r.json()).then(data => {
                if (!data.band) return;
                document.querySelectorAll('.band-btn').forEach(btn => {
                    if (btn.textContent.trim() === data.band) {
                        btn.style.background = '#cc0000';
                        btn.style.boxShadow  = '0 0 8px rgba(255,0,0,0.6)';
                    }
                });
            }).catch(console.error);
        });
    </script>
    )rawliteral";
    html += ALERT_HTML;
    html += "</body></html>";
    server.send(200, "text/html", html);
}

// ─── /setband ─────────────────────────────────────────────────────────────────

void handleSetBand() {
    if (!server.hasArg("band")) { server.send(400, "text/plain", "Missing band parameter"); return; }
    String band = server.arg("band");
    memset(state.band,  0, sizeof(state.band));
    memset(status.band, 0, sizeof(status.band));
    strncpy(state.band,  band.c_str(), sizeof(state.band)  - 1);
    strncpy(status.band, band.c_str(), sizeof(status.band) - 1);
    Serial.print("Band set to: "); Serial.println(state.band);
    sendStateData();
    server.send(200, "text/plain", "OK");
}

// ─── /setstate ────────────────────────────────────────────────────────────────

void handleSetState() {
    if (!server.hasArg("state")) { server.send(400, "text/plain", "Missing state parameter"); return; }
    state.state = (server.arg("state") == "true");
    Serial.print("State set to: "); Serial.println(state.state ? "ON" : "OFF");
    sendStateData();
    server.send(200, "text/plain", "OK");
}

// ─── /settings ────────────────────────────────────────────────────────────────

void handleSettingsPage() {
    String html = pageHead("Settings");
    html += R"rawliteral(
    <div class="container">
        <h1>Amplifier Settings</h1>
        <form id="settingsForm" action="/savesettings" method="POST">
            <div class="settings-grid">
                <div class="settings-item"><label>Max SWR:</label>
                    <input type="number" id="max_swr" name="max_swr" step="1" min="1" max="10" value="0"></div>
                <div class="settings-item"><label>Max Current (A):</label>
                    <input type="number" id="max_current" name="max_current" step="1" min="1" max="50" value="0"></div>
                <div class="settings-item"><label>Max Voltage (V):</label>
                    <input type="number" id="max_voltage" name="max_voltage" step="1" min="1" max="60" value="0"></div>
                <div class="settings-item"><label>Max Plate Temp (°C):</label>
                    <input type="number" id="max_plate_temp" name="max_plate_temp" step="1" min="10" max="100" value="0"></div>
                <div class="settings-item"><label>Max Water Temp (°C):</label>
                    <input type="number" id="max_water_temp" name="max_water_temp" step="1" min="10" max="100" value="0"></div>
                <div class="settings-item"><label>Max Pump Speed Temp (°C):</label>
                    <input type="number" id="max_pump_speed_temp" name="max_pump_speed_temp" step="1" min="10" max="100" value="0"></div>
                <div class="settings-item"><label>Min Pump Speed Temp (°C):</label>
                    <input type="number" id="min_pump_speed_temp" name="min_pump_speed_temp" step="1" min="10" max="100" value="0"></div>
                <div class="settings-item"><label>Max Fan Speed Temp (°C):</label>
                    <input type="number" id="max_fan_speed_temp" name="max_fan_speed_temp" step="1" min="10" max="100" value="0"></div>
                <div class="settings-item"><label>Min Fan Speed Temp (°C):</label>
                    <input type="number" id="min_fan_speed_temp" name="min_fan_speed_temp" step="1" min="10" max="100" value="0"></div>
                <div class="settings-item"><label>Min efficiency coefficient:</label>
                    <input type="number" id="min_coeff" name="min_coeff" step="1" min="30" max="90" value="0"></div>
                <div class="settings-item"><label>Maximum input power:</label>
                    <input type="number" id="max_input_power" name="max_input_power" step="1" min="1" max="10" value="0"></div>
                <div class="settings-item"><label>Protection Enabled:</label>
                    <div class="toggle-switch">
                        <input type="checkbox" id="protection_enabled" name="protection_enabled" class="toggle-input">
                        <label for="protection_enabled" class="toggle-label"></label>
                    </div>
                </div>
                <div class="settings-item"><label>Default Band:</label>
                    <select id="default_band" name="default_band">
                        <option value="160m">160m</option><option value="80m">80m</option>
                        <option value="60m">60m</option><option value="40m">40m</option>
                        <option value="30m">30m</option><option value="20m">20m</option>
                        <option value="17m">17m</option><option value="15m">15m</option>
                        <option value="12m">12m</option><option value="10m">10m</option>
                        <option value="6m">6m</option>
                    </select>
                </div>
                <div class="settings-item"><label>Auto Band:</label>
                    <div class="toggle-switch">
                        <input type="checkbox" id="autoband" name="autoband" class="toggle-input">
                        <label for="autoband" class="toggle-label"></label>
                    </div>
                </div>
            </div>
            <div class="navigation">
                <button type="button" onclick="location.href='/'">Back to Status</button>
                <button type="submit">Save Settings</button>
                <span id="message" class="message"></span>
            </div>
        </form>
    </div>
    <script>
    )rawliteral";
    html += ALERT_JS;
    html += R"rawliteral(
        function loadSettings(attempt) {
            attempt = attempt || 1;
            const msg = document.getElementById('message');
            msg.style.color = '#28a745';
            msg.textContent = attempt > 1 ? ('Retrying... (' + attempt + ')') : 'Loading...';
            fetch('/getsettings')
            .then(r => {
                if (!r.ok) throw new Error('HTTP ' + r.status);
                return r.json();
            })
            .then(data => {
                ['max_swr','max_current','max_voltage','max_plate_temp','max_water_temp',
                'max_pump_speed_temp','min_pump_speed_temp','max_fan_speed_temp',
                'min_fan_speed_temp','min_coeff','max_input_power'].forEach(id => {
                    document.getElementById(id).value = data[id];
                });
                document.getElementById('protection_enabled').checked = data.protection_enabled;
                document.getElementById('autoband').checked = data.autoband;
                const sel = document.getElementById('default_band');
                for (let i = 0; i < sel.options.length; i++) {
                    if (sel.options[i].value === data.default_band) { sel.selectedIndex = i; break; }
                }
                msg.textContent = '';
            })
            .catch(e => {
                if (attempt < 5) {
                    setTimeout(() => loadSettings(attempt + 1), 500 * attempt);
                } else {
                    msg.style.color = '#dc3545';
                    msg.textContent = 'Load failed. ';
                    const btn = document.createElement('button');
                    btn.type = 'button';
                    btn.textContent = 'Retry';
                    btn.onclick = () => { msg.style.color = '#28a745'; msg.innerHTML = ''; loadSettings(1); };
                    msg.appendChild(btn);
                }
            });
        }
        document.addEventListener('DOMContentLoaded', () => loadSettings(1));
        document.getElementById('settingsForm').addEventListener('submit', () => {
            document.getElementById('message').style.color = '#28a745';
            document.getElementById('message').textContent = 'Saving...';
        });
    </script>
    )rawliteral";
    html += ALERT_HTML;
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleGetSettings() {
    requestAndWaitForSettings(300);
    if (waitingForResponse != RESPONSE_NONE) {
        server.send(503, "application/json", "{\"error\":\"Busy\"}");
        return;
    }
    String r = "{";
    r += "\"max_swr\":"             + String(settings.max_swr)             + ",";
    r += "\"max_current\":"         + String(settings.max_current)         + ",";
    r += "\"max_voltage\":"         + String(settings.max_voltage)         + ",";
    r += "\"max_plate_temp\":"      + String(settings.max_plate_temp)      + ",";
    r += "\"max_water_temp\":"      + String(settings.max_water_temp)      + ",";
    r += "\"max_pump_speed_temp\":" + String(settings.max_pump_speed_temp) + ",";
    r += "\"min_pump_speed_temp\":" + String(settings.min_pump_speed_temp) + ",";
    r += "\"max_fan_speed_temp\":"  + String(settings.max_fan_speed_temp)  + ",";
    r += "\"min_fan_speed_temp\":"  + String(settings.min_fan_speed_temp)  + ",";
    r += "\"min_coeff\":"           + String(settings.min_coeff)              + ",";
    r += "\"max_input_power\":"     + String(settings.max_input_power)        + ",";
    r += "\"protection_enabled\":"  + String(status.protection_enabled ? "true":"false") + ",";
    r += "\"autoband\":"            + String(settings.autoband ? "true":"false") + ",";
    r += "\"default_band\":\""      + String(settings.default_band) + "\"";
    r += "}";
    server.send(200, "application/json", r);
}

void handleSaveSettings() {
    if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
    if (server.hasArg("max_swr"))            settings.max_swr             = server.arg("max_swr").toInt();
    if (server.hasArg("max_current"))        settings.max_current         = server.arg("max_current").toInt();
    if (server.hasArg("max_voltage"))        settings.max_voltage         = server.arg("max_voltage").toInt();
    if (server.hasArg("max_plate_temp"))     settings.max_plate_temp      = server.arg("max_plate_temp").toInt();
    if (server.hasArg("max_water_temp"))     settings.max_water_temp      = server.arg("max_water_temp").toInt();
    if (server.hasArg("max_pump_speed_temp"))settings.max_pump_speed_temp = server.arg("max_pump_speed_temp").toInt();
    if (server.hasArg("min_pump_speed_temp"))settings.min_pump_speed_temp = server.arg("min_pump_speed_temp").toInt();
    if (server.hasArg("max_fan_speed_temp")) settings.max_fan_speed_temp  = server.arg("max_fan_speed_temp").toInt();
    if (server.hasArg("min_fan_speed_temp")) settings.min_fan_speed_temp  = server.arg("min_fan_speed_temp").toInt();
    if (server.hasArg("min_coeff"))          settings.min_coeff           = server.arg("min_coeff").toInt();
    if (server.hasArg("max_input_power"))    settings.max_input_power     = server.arg("max_input_power").toInt();
    status.protection_enabled = server.hasArg("protection_enabled");
    settings.autoband         = server.hasArg("autoband");
    if (server.hasArg("default_band")) {
        String b = server.arg("default_band");
        strncpy(settings.default_band, b.c_str(), sizeof(settings.default_band) - 1);
        settings.default_band[sizeof(settings.default_band)-1] = '\0';
    }

    if (settings.max_pump_speed_temp < settings.min_pump_speed_temp)
        settings.max_pump_speed_temp = settings.min_pump_speed_temp;
    if (settings.max_fan_speed_temp < settings.min_fan_speed_temp)
        settings.max_fan_speed_temp = settings.min_fan_speed_temp;

    sendStateData();
    sendSettingsData();
    server.sendHeader("Location", "/"); server.send(303);
}

// ─── /calibration ─────────────────────────────────────────────────────────────

void handleCalibrationPage() {
    String html = pageHead("Calibration");
    html += R"rawliteral(
    <div class="container">
        <h1>Calibration Settings</h1>
        <form id="calibrationForm" action="/savecalibration" method="POST">
            <div class="settings-grid">
                <div class="settings-item"><label>Low Band FWD Coefficient:</label>
                    <input type="number" id="low_fwd_coeff"  name="low_fwd_coeff"  step="0.0001" min="1" max="1000" value="0"></div>
                <div class="settings-item"><label>Low Band REV Coefficient:</label>
                    <input type="number" id="low_rev_coeff"  name="low_rev_coeff"  step="0.0001" min="1" max="1000" value="0"></div>
                <div class="settings-item"><label>Low Band IFWD Coefficient:</label>
                    <input type="number" id="low_ifwd_coeff" name="low_ifwd_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                <div class="settings-item"><label>Mid Band FWD Coefficient:</label>
                    <input type="number" id="mid_fwd_coeff"  name="mid_fwd_coeff"  step="0.0001" min="1" max="1000" value="0"></div>
                <div class="settings-item"><label>Mid Band REV Coefficient:</label>
                    <input type="number" id="mid_rev_coeff"  name="mid_rev_coeff"  step="0.0001" min="1" max="1000" value="0"></div>
                <div class="settings-item"><label>Mid Band IFWD Coefficient:</label>
                    <input type="number" id="mid_ifwd_coeff" name="mid_ifwd_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                <div class="settings-item"><label>High Band FWD Coefficient:</label>
                    <input type="number" id="high_fwd_coeff"  name="high_fwd_coeff"  step="0.0001" min="1" max="1000" value="0"></div>
                <div class="settings-item"><label>High Band REV Coefficient:</label>
                    <input type="number" id="high_rev_coeff"  name="high_rev_coeff"  step="0.0001" min="1" max="1000" value="0"></div>
                <div class="settings-item"><label>High Band IFWD Coefficient:</label>
                    <input type="number" id="high_ifwd_coeff" name="high_ifwd_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                <div class="settings-item"><label>Voltage Coefficient:</label>
                    <input type="number" id="voltage_coeff" name="voltage_coeff" step="0.0001" min="1" max="100" value="0"></div>
                <div class="settings-item"><label>Current Coefficient:</label>
                    <input type="number" id="current_coeff" name="current_coeff" step="0.0001" min="0.1" max="100" value="0"></div>
                <div class="settings-item"><label>Reserve Coefficient:</label>
                    <input type="number" id="rsrv_coeff" name="rsrv_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                <div class="settings-item"><label>Current sensor zero voltage:</label>
                    <input type="number" id="acs_zero" name="acs_zero" step="0.0001" min="0" max="10" value="0"></div>
                <div class="settings-item"><label>Current sensor sensitivity:</label>
                    <input type="number" id="acs_sens" name="acs_sens" step="0.0001" min="0" max="10" value="0"></div>
            </div>
            <div class="navigation">
                <button type="button" onclick="location.href='/'">Back to Status</button>
                <button type="submit">Save Calibration</button>
                <span id="message" class="message"></span>
            </div>
        </form>
    </div>
    <script>
    )rawliteral";
    html += ALERT_JS;
    html += R"rawliteral(
        function loadCalibration(attempt) {
            attempt = attempt || 1;
            const msg = document.getElementById('message');
            msg.style.color = '#28a745';
            msg.textContent = attempt > 1 ? ('Retrying... (' + attempt + ')') : 'Loading...';
            fetch('/getcalibration')
            .then(r => {
                if (!r.ok) throw new Error('HTTP ' + r.status);
                return r.json();
            })
            .then(data => {
                ['low_fwd_coeff','low_rev_coeff','low_ifwd_coeff',
                'mid_fwd_coeff','mid_rev_coeff','mid_ifwd_coeff',
                'high_fwd_coeff','high_rev_coeff','high_ifwd_coeff',
                'voltage_coeff','current_coeff','rsrv_coeff','acs_zero','acs_sens']
                .forEach(id => { document.getElementById(id).value = data[id]; });
                msg.textContent = '';
            })
            .catch(e => {
                if (attempt < 5) {
                    setTimeout(() => loadCalibration(attempt + 1), 500 * attempt);
                } else {
                    msg.style.color = '#dc3545';
                    msg.textContent = 'Load failed. ';
                    const btn = document.createElement('button');
                    btn.type = 'button';
                    btn.textContent = 'Retry';
                    btn.onclick = () => { msg.style.color = '#28a745'; msg.innerHTML = ''; loadCalibration(1); };
                    msg.appendChild(btn);
                }
            });
        }
        document.addEventListener('DOMContentLoaded', () => loadCalibration(1));
        document.getElementById('calibrationForm').addEventListener('submit', () => {
            document.getElementById('message').style.color = '#28a745';
            document.getElementById('message').textContent = 'Saving calibration...';
        });
    </script>
    )rawliteral";
    html += ALERT_HTML;
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleGetCalibration() {
    if (!requestAndWaitForCalibration(300)) {
        server.send(503, "application/json", "{\"error\":\"Busy or timeout\"}");
        return;
    }
    String r = "{";
    r += "\"low_fwd_coeff\":"  + String(calibration.low_fwd_coeff,  4) + ",";
    r += "\"low_rev_coeff\":"  + String(calibration.low_rev_coeff,  4) + ",";
    r += "\"low_ifwd_coeff\":" + String(calibration.low_ifwd_coeff, 4) + ",";
    r += "\"mid_fwd_coeff\":"  + String(calibration.mid_fwd_coeff,  4) + ",";
    r += "\"mid_rev_coeff\":"  + String(calibration.mid_rev_coeff,  4) + ",";
    r += "\"mid_ifwd_coeff\":" + String(calibration.mid_ifwd_coeff, 4) + ",";
    r += "\"high_fwd_coeff\":" + String(calibration.high_fwd_coeff, 4) + ",";
    r += "\"high_rev_coeff\":" + String(calibration.high_rev_coeff, 4) + ",";
    r += "\"high_ifwd_coeff\":"+ String(calibration.high_ifwd_coeff,4) + ",";
    r += "\"voltage_coeff\":"  + String(calibration.voltage_coeff,  4) + ",";
    r += "\"current_coeff\":"  + String(calibration.current_coeff,  4) + ",";
    r += "\"rsrv_coeff\":"     + String(calibration.rsrv_coeff,     4) + ",";
    r += "\"acs_zero\":"       + String(calibration.acs_zero,       4) + ",";
    r += "\"acs_sens\":"       + String(calibration.acs_sens,       4);
    r += "}";
    server.send(200, "application/json", r);
}

void handleSaveCalibration() {
    if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
    if (server.hasArg("low_fwd_coeff"))  calibration.low_fwd_coeff  = server.arg("low_fwd_coeff").toFloat();
    if (server.hasArg("low_rev_coeff"))  calibration.low_rev_coeff  = server.arg("low_rev_coeff").toFloat();
    if (server.hasArg("low_ifwd_coeff")) calibration.low_ifwd_coeff = server.arg("low_ifwd_coeff").toFloat();
    if (server.hasArg("mid_fwd_coeff"))  calibration.mid_fwd_coeff  = server.arg("mid_fwd_coeff").toFloat();
    if (server.hasArg("mid_rev_coeff"))  calibration.mid_rev_coeff  = server.arg("mid_rev_coeff").toFloat();
    if (server.hasArg("mid_ifwd_coeff")) calibration.mid_ifwd_coeff = server.arg("mid_ifwd_coeff").toFloat();
    if (server.hasArg("high_fwd_coeff")) calibration.high_fwd_coeff = server.arg("high_fwd_coeff").toFloat();
    if (server.hasArg("high_rev_coeff")) calibration.high_rev_coeff = server.arg("high_rev_coeff").toFloat();
    if (server.hasArg("high_ifwd_coeff"))calibration.high_ifwd_coeff= server.arg("high_ifwd_coeff").toFloat();
    if (server.hasArg("voltage_coeff"))  calibration.voltage_coeff  = server.arg("voltage_coeff").toFloat();
    if (server.hasArg("current_coeff"))  calibration.current_coeff  = server.arg("current_coeff").toFloat();
    if (server.hasArg("rsrv_coeff"))     calibration.rsrv_coeff     = server.arg("rsrv_coeff").toFloat();
    if (server.hasArg("acs_zero"))       calibration.acs_zero       = server.arg("acs_zero").toFloat();
    if (server.hasArg("acs_sens"))       calibration.acs_sens       = server.arg("acs_sens").toFloat();
    sendCalibrationData();
    server.sendHeader("Location", "/"); server.send(303);
}

// ─── /style.css ───────────────────────────────────────────────────────────────

void handleCSS() {
    // (CSS unchanged from original — stored as raw literal for clarity)
    static const char css[] PROGMEM = R"css(
body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background-color: #f0f0f0; }
.container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
h1 { text-align: center; color: #333; margin-bottom: 20px; }
.status-grid, .settings-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin: 20px 0; }
.status-item, .settings-item { display: flex; flex-direction: column; padding: 15px; background: #f8f9fa; border-radius: 8px; border: 1px solid #e9ecef; }
.status-item label, .settings-item label { font-weight: bold; color: #495057; font-size: 0.9em; margin-bottom: 8px; }
.settings-item input, .settings-item select { padding: 8px 12px; border: 1px solid #ced4da; border-radius: 4px; font-size: 1em; background: white; }
.settings-item input:focus, .settings-item select:focus { outline: none; border-color: #007AFF; box-shadow: 0 0 0 2px rgba(0,122,255,0.2); }
.bar { width: 100%; height: 20px; background: #e0e0e0; border-radius: 10px; margin-top: 5px; overflow: hidden; }
.bar-fill { height: 100%; background: #007AFF; border-radius: 10px; transition: all 0.3s ease; width: 0%; }
.toggle-switch { display: flex; align-items: center; margin-top: 5px; }
.toggle-input { display: none; }
.toggle-label { position: relative; display: inline-block; width: 50px; height: 25px; background-color: #ccc; border-radius: 25px; cursor: pointer; transition: background-color 0.3s; }
.toggle-label:before { content: ""; position: absolute; width: 21px; height: 21px; border-radius: 50%; background-color: white; top: 2px; left: 2px; transition: transform 0.3s; }
.toggle-input:checked + .toggle-label { background-color: #28a745; }
.toggle-input:checked + .toggle-label:before { transform: translateX(25px); }
.band-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin: 20px 0; }
.band-btn, .navigation button { padding: 12px 20px; border: none; background: #007AFF; color: white; border-radius: 6px; cursor: pointer; font-size: 1em; transition: all 0.3s ease; text-align: center; }
.band-btn:hover, .navigation button:hover { background: #0056cc; transform: translateY(-1px); box-shadow: 0 2px 5px rgba(0,0,0,0.2); }
.navigation { display: flex; justify-content: space-between; align-items: center; margin-top: 25px; padding-top: 20px; border-top: 1px solid #e9ecef; }
.navigation button { margin: 0 5px; }
.navigation button:first-child { background: #6c757d; }
.navigation button:first-child:hover { background: #545b62; }
.navigation button:last-child { background: #28a745; }
.navigation button:last-child:hover { background: #218838; }
.message { color: #28a745; font-weight: bold; text-align: center; margin: 10px 0; min-height: 20px; }
.alert-overlay { position: fixed; top: 0; left: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.7); display: none; justify-content: center; align-items: center; z-index: 1000; }
.alert-modal { background: white; padding: 30px; border-radius: 15px; box-shadow: 0 10px 30px rgba(0,0,0,0.3); max-width: 400px; width: 90%; text-align: center; border: 3px solid #ff4444; }
.alert-title { color: #ff4444; font-size: 24px; font-weight: bold; margin-bottom: 15px; }
.alert-message { color: #333; font-size: 16px; margin-bottom: 25px; line-height: 1.4; }
.alert-button { background: #ff4444; color: white; border: none; padding: 12px 30px; border-radius: 6px; font-size: 16px; cursor: pointer; transition: background 0.3s ease; }
.alert-button:hover { background: #cc0000; }
.alert-visible { display: flex !important; }
body.ptt-on { background-color: #ffdddd; }
body.ptt-on .container { border: 2px solid #ff0000; }
.status-grid.ptt-on { background: #ffdddd; border: 2px solid #ff0000; border-radius: 10px; padding: 10px; }
.container.ptt-on { background: #ffdddd; border: 2px solid #ff0000; }
@media (max-width: 768px) {
    .status-grid, .settings-grid { grid-template-columns: 1fr; }
    .band-grid { grid-template-columns: repeat(2,1fr); }
    .navigation { flex-direction: column; gap: 10px; }
    .navigation button { width: 100%; margin: 5px 0; }
}
@media (max-width: 480px) {
    body { padding: 10px; }
    .container { padding: 15px; }
    .band-grid { grid-template-columns: 1fr; }
}
)css";
    server.send(200, "text/css", css);
}

// ─── Route registration ───────────────────────────────────────────────────────

void registerWebRoutes() {
    server.on("/",               HTTP_GET,  handleRoot);
    server.on("/status",         HTTP_GET,  handleStatus);
    server.on("/band",           HTTP_GET,  handleBandPage);
    server.on("/settings",       HTTP_GET,  handleSettingsPage);
    server.on("/getsettings",    HTTP_GET,  handleGetSettings);
    server.on("/savesettings",   HTTP_POST, handleSaveSettings);
    server.on("/setband",        HTTP_POST, handleSetBand);
    server.on("/setstate",       HTTP_POST, handleSetState);
    server.on("/style.css",      HTTP_GET,  handleCSS);
    server.on("/calibration",    HTTP_GET,  handleCalibrationPage);
    server.on("/getcalibration", HTTP_GET,  handleGetCalibration);
    server.on("/savecalibration",HTTP_POST, handleSaveCalibration);
    server.on("/resetalert",     HTTP_POST, handleResetAlert);
    server.on("/reseterror",     HTTP_POST, handleResetError);
    server.on("/events",         HTTP_GET,  handleEvents);
}
