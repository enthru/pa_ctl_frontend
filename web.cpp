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
        // Same 14 keys as ALARM_TEXTS in pa_ctl.ino (the backend reason dictionary).
        const map = {
            'no_output':      'Current, no output',
            'cur_sensor':     'Current sensor failed',
            'volt_sensor':    'Voltage sensor failed',
            'eff_high':       'Efficiency impossibly high',
            'swr':            'High SWR',
            'voltage':        'Overvoltage',
            'current':        'Overcurrent',
            'ipower':         'Input overdrive',
            'water_temp':     'Water overheating',
            'plate_temp':     'Plate overheating',
            'coeff':          'Low efficiency',
            'band':           'Unknown band',
            'wrong_band':     'Wrong band',
            'unk_freq_on_tx': 'Frequency out of band'
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
// Emits <head> plus the shared top bar; `path` marks the current page's nav link.
static String pageHead(const char* title, const char* path) {
    static const char* const NAV[][2] = {
        {"/", "Status"}, {"/band", "Band"}, {"/settings", "Settings"}, {"/calibration", "Calibration"},
    };
    String h = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>";
    h += title;
    h += R"(</title><meta name="viewport" content="width=device-width, initial-scale=1">)";
    h += R"(<meta name="theme-color" content="#0e1115">)";
    h += R"(<link rel="stylesheet" href="/style.css"></head><body>)";
    h += R"(<header class="top"><div class="brand">PA<span>CTL</span></div><nav>)";
    for (auto &n : NAV) {
        h += "<a href=\""; h += n[0]; h += '"';
        if (!strcmp(n[0], path)) h += " class=\"on\"";
        h += '>'; h += n[1]; h += "</a>";
    }
    h += "</nav></header>";
    return h;
}

// ─── /events ─────────────────────────────────────────────────────────────────

void handleEvents() {
    char r[192];
    snprintf(r, sizeof(r),
        "{\"alarm_active\":%s,\"alert_reason\":\"%s\",\"has_error\":%s,\"error_message\":\"%s\"}",
        status.alarm ? "true" : "false",
        status.alert_reason,
        hasWebError ? "true" : "false",
        webErrorMessage.c_str());
    server.send(200, "application/json", r);
    // The error is left set until a client explicitly clears it via /reseterror
    // (closeError()). Auto-clearing here would hide it from every polling
    // client except the first one.
}

// ─── /resetalert, /reseterror ─────────────────────────────────────────────────

void handleResetAlert() {
    state.alarm  = false; status.alarm = false;
    status.ptt   = false; state.ptt    = false;
    state.state  = false; status.state = false;
    sendStateData();
    // Reset is a safety action and is always allowed. Respond immediately; the
    // screen returns to ui_main via the alarm-routing logic in loop() once the
    // backend confirms — no need to block the web handler while it settles.
    server.send(200, "text/plain", "OK");
}

void handleResetError() {
    hasWebError     = false;
    webErrorMessage = "";
    server.send(200, "text/plain", "OK");
}

// ─── / (main status page) ─────────────────────────────────────────────────────

void handleRoot() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", pageHead("PA Status", "/"));
    server.sendContent_P(R"rawliteral(
    <main class="wrap">
        <div class="statusbar">
            <span class="chip">BAND <b id="band">-</b></span>
            <span class="chip" id="txChip">RX</span>
            <span class="chip" id="protChip">PROT</span>
            <span class="chip" id="linkChip">LINK</span>
            <label class="amp-switch">AMP
                <span class="toggle-switch">
                    <input type="checkbox" id="stateToggle" class="toggle-input">
                    <label for="stateToggle" class="toggle-label"></label>
                </span>
            </label>
        </div>
        <div id="debugBanner" class="debug-banner" hidden>DEBUG MODE &mdash; raw ADC input voltages, not calibrated readings</div>
        <section class="gauges">
            <div class="card gauge" id="gPwr"></div>
            <div class="card gauge" id="gSwr"></div>
            <div class="card gauge" id="gWat"></div>
            <div class="card gauge" id="gPlt"></div>
        </section>
        <section class="cards">
            <div class="card"><h2>RF</h2>
                <div class="stat"><span id="refLbl">Reflected</span><b id="ref">-</b></div>
                <div class="stat"><span id="drvLbl">Input drive</span><b id="trxfwd">-</b></div>
                <div class="meter"><i id="drvBar"></i></div>
                <div class="stat"><span>Efficiency</span><b id="coeff">-</b></div>
                <div class="stat"><span>Dissipation</span><b id="heat">-</b></div>
            </div>
            <div class="card"><h2>Power supply</h2>
                <div class="stat"><span id="volLbl">Voltage</span><b id="vol">-</b></div>
                <div class="meter"><i id="volBar"></i></div>
                <div class="stat"><span id="curLbl">Current</span><b id="cur">-</b></div>
                <div class="meter"><i id="curBar"></i></div>
                <div class="stat"><span>DC input</span><b id="dcin">-</b></div>
                <div class="stat" id="rsrvItem" hidden><span>Reserve (ADC ch5)</span><b id="rsrv">-</b></div>
            </div>
            <div class="card"><h2>Cooling</h2>
                <div class="stat"><span>Fan</span><b id="pwm_cooler">-</b></div>
                <div class="meter"><i id="fanBar"></i></div>
                <div class="stat"><span>Pump</span><b id="pwm_pump">-</b></div>
                <div class="meter"><i id="pumpBar"></i></div>
            </div>
            <div class="card wide"><h2>Trend <small>last 100 s</small></h2>
                <div class="trends">
                    <div><div class="t-head"><span class="l-pwr">Output power</span><b id="tPwrMax"></b></div>
                        <svg viewBox="0 0 200 60" preserveAspectRatio="none"><polyline id="tPwr" class="t-pwr"/></svg></div>
                    <div><div class="t-head"><span><span class="l-wat">Water</span> &middot; <span class="l-plt">Plate</span></span><b id="tTmpRange"></b></div>
                        <svg viewBox="0 0 200 60" preserveAspectRatio="none"><polyline id="tWat" class="t-wat"/><polyline id="tPlt" class="t-plt"/></svg></div>
                </div>
            </div>
        </section>
    </main>
    <script>
    )rawliteral");
    server.sendContent_P(ALERT_JS);
    server.sendContent_P(R"rawliteral(
        const $ = id => document.getElementById(id);
        // Protection limits from /getsettings, used to scale and colour the readouts the
        // same way the display does (warn near the limit, red past it). null until loaded:
        // everything renders uncoloured meanwhile.
        let lim = null;
        function loadLimits(n) {
            fetch('/getsettings').then(r => { if (!r.ok) throw 0; return r.json(); })
            .then(s => { lim = s; })
            .catch(() => { if (n < 5) setTimeout(() => loadLimits(n + 1), 1000 * n); });
        }
        // '' = no limit known, otherwise ok/warn/bad. floor=true for a lower bound (efficiency).
        function level(v, warnAt, alarmAt, floor) {
            if (!alarmAt) return '';
            if (floor) return v <= alarmAt ? 'bad' : v <= warnAt ? 'warn' : 'ok';
            return v >= alarmAt ? 'bad' : v >= warnAt ? 'warn' : 'ok';
        }

        // Semicircular gauge: centre (60,62), r 48, sweeping left to right over the top.
        const ARC = 'M12 62A48 48 0 0 1 108 62';
        function gauge(id) {
            $(id).innerHTML = '<svg viewBox="0 0 120 74">'
                + '<path class="g-track" d="' + ARC + '" pathLength="100"/>'
                + '<path class="g-val" d="' + ARC + '" pathLength="100" stroke-dasharray="0 100"/>'
                + '<line class="g-lim"/>'
                + '<text class="g-num" x="60" y="52">-</text><text class="g-unit" x="60" y="64"></text>'
                + '<text class="g-sc" x="12" y="73"></text><text class="g-sc" x="108" y="73"></text>'
                + '</svg><div class="g-title"></div>';
        }
        function setGauge(id, title, v, min, max, text, unit, cls, limAt) {
            const el = $(id), f = Math.max(0, Math.min(1, (v - min) / (max - min)));
            const p = el.querySelector('.g-val');
            p.setAttribute('stroke-dasharray', (f * 100).toFixed(1) + ' 100');
            p.setAttribute('class', 'g-val ' + (cls || ''));
            el.querySelector('.g-num').textContent  = text;
            el.querySelector('.g-unit').textContent = unit;
            const sc = el.querySelectorAll('.g-sc');
            sc[0].textContent = min; sc[1].textContent = max;
            el.querySelector('.g-title').textContent = title;
            // Tick across the arc where the protection limit sits.
            const l = el.querySelector('.g-lim');
            if (limAt > min && limAt <= max) {
                const a = Math.PI * (1 - (limAt - min) / (max - min)), c = Math.cos(a), s = Math.sin(a);
                l.setAttribute('x1', 60 + 41 * c); l.setAttribute('y1', 62 - 41 * s);
                l.setAttribute('x2', 60 + 55 * c); l.setAttribute('y2', 62 - 55 * s);
                l.style.display = '';
            } else l.style.display = 'none';
        }
        function setBar(id, v, max, cls) {
            const b = $(id);
            b.style.width = Math.max(0, Math.min(100, v / max * 100)) + '%';
            b.className = cls || '';
        }
        function setText(id, text, cls) { const e = $(id); e.textContent = text; e.className = cls || ''; }

        // Client-side history, one sample per second, like the display's 100 s sparklines.
        const N = 100, hPwr = [], hWat = [], hPlt = [];
        let lastPush = 0;
        function push(a, v) { a.push(v); if (a.length > N) a.shift(); }
        function plot(id, a, lo, hi) {
            const k = 58 / ((hi - lo) || 1), x0 = N - a.length;
            $(id).setAttribute('points', a.map((v, i) =>
                ((x0 + i) * 200 / (N - 1)).toFixed(1) + ',' + (59 - (v - lo) * k).toFixed(1)).join(' '));
        }
        function drawTrends() {
            const pMax = Math.max(100, ...hPwr);
            plot('tPwr', hPwr, 0, pMax);
            $('tPwrMax').textContent = hPwr.length ? 'peak ' + Math.max(...hPwr).toFixed(0) + ' W' : '';
            const t = hWat.concat(hPlt);
            if (!t.length) return;
            const lo = Math.floor(Math.min(...t) - 2), hi = Math.ceil(Math.max(...t) + 2);
            plot('tWat', hWat, lo, hi); plot('tPlt', hPlt, lo, hi);
            $('tTmpRange').textContent = lo + '–' + hi + ' °C';
        }

        function render(d) {
            const dbg = !!d.debug, tx = !!d.ptt, L = lim || {};
            document.body.classList.toggle('debug-on', dbg);
            document.body.classList.toggle('ptt-on', tx);
            $('debugBanner').hidden = !dbg;
            $('rsrvItem').hidden    = !dbg;

            $('band').textContent = d.band || '-';
            setText('txChip', tx ? 'TX' : 'RX', 'chip ' + (tx ? 'tx' : 'ok'));
            const prot = d.alarm ? 2 : d.protection_enabled ? 1 : 0;
            setText('protChip', ['PROT: OFF', 'PROT: ON', 'ALARM'][prot], 'chip ' + ['off', 'ok', 'bad'][prot]);
            $('stateToggle').checked = !!d.state;

            const fwd = d.fwd || 0, ref = d.ref || 0, drv = d.trxfwd || 0;
            const vol = d.voltage || 0, cur = d.current || 0, swr = d.swr || 0, eff = d.coeff || 0;
            // Debug mode: the ADC-fed fields are raw volts at the backend pins. Retag them
            // with their channel, rescale to the ADC's 3.3 V full scale and drop the threshold
            // colouring and the derived SWR/efficiency/dissipation — "1.70 W" for what is
            // really 1.70 V would be badly misleading on an amplifier this size.
            if (dbg) {
                const V = x => (x || 0).toFixed(4) + ' V';
                setGauge('gPwr', 'FWD det (ch0)', fwd, 0, 3.3, fwd.toFixed(3), 'V', 'acc', 0);
                setGauge('gSwr', 'SWR', 1, 1, 5, '--', '', '', 0);
                $('refLbl').textContent = 'REV det (ch1)';
                $('drvLbl').textContent = 'Input FWD det (ch2)';
                $('volLbl').textContent = 'V sense (ch3)';
                $('curLbl').textContent = 'I sense (ch4)';
                setText('ref', V(ref)); setText('trxfwd', V(drv));
                setText('vol', V(vol)); setText('cur', V(cur)); setText('rsrv', V(d.rsrv));
                setText('coeff', '--'); setText('heat', '--'); setText('dcin', '--');
                setBar('drvBar', drv, 3.3); setBar('volBar', vol, 3.3); setBar('curBar', cur, 3.3);
            } else {
                const swrMax = L.max_swr ? Math.max(3, L.max_swr + 1) : 5;
                setGauge('gPwr', 'Output power', fwd, 0, 1200, fwd.toFixed(0), 'W', tx ? 'tx' : 'acc', 0);
                setGauge('gSwr', 'SWR', Math.max(1, swr), 1, swrMax, swr.toFixed(2), '',
                         level(swr, 0.8 * L.max_swr, L.max_swr), L.max_swr);
                $('refLbl').textContent = 'Reflected';
                $('drvLbl').textContent = 'Input drive';
                $('volLbl').textContent = 'Voltage';
                $('curLbl').textContent = 'Current';
                const drvC = level(drv, 0.9 * L.max_input_power, L.max_input_power);
                const volC = level(vol, 0.95 * L.max_voltage, L.max_voltage);
                const curC = level(cur, 0.9 * L.max_current, L.max_current);
                setText('ref', ref.toFixed(2) + ' W');
                setText('trxfwd', drv.toFixed(2) + ' W', drvC);
                setText('vol', vol.toFixed(1) + ' V', volC);
                setText('cur', cur.toFixed(1) + ' A', curC);
                // Efficiency only means something under drive; in RX it reads 0 and would
                // otherwise sit permanently red against its floor.
                setText('coeff', eff.toFixed(1) + ' %', tx ? level(eff, 1.15 * L.min_coeff, L.min_coeff, true) : '');
                setText('heat', Math.max(0, vol * cur - fwd).toFixed(0) + ' W');
                setText('dcin', (vol * cur).toFixed(0) + ' W');
                setBar('drvBar', drv, (L.max_input_power || 10) * 1.2, drvC);
                setBar('volBar', vol, (L.max_voltage || 60) * 1.2, volC);
                setBar('curBar', cur, (L.max_current || 50) * 1.2, curC);
            }
            // Temperatures come from the DS18B20s, not the ADC — unaffected by debug mode.
            const wt = d.water_temp || 0, pt = d.plate_temp || 0;
            setGauge('gWat', 'Water', wt, 0, 100, wt.toFixed(1), '°C',
                     level(wt, 0.9 * L.max_water_temp, L.max_water_temp) || 'ok', L.max_water_temp);
            setGauge('gPlt', 'Plate', pt, 0, 100, pt.toFixed(1), '°C',
                     level(pt, 0.9 * L.max_plate_temp, L.max_plate_temp) || 'ok', L.max_plate_temp);
            setText('pwm_cooler', (d.pwm_cooler || 0) + ' %'); setBar('fanBar',  d.pwm_cooler || 0, 100);
            setText('pwm_pump',   (d.pwm_pump   || 0) + ' %'); setBar('pumpBar', d.pwm_pump   || 0, 100);

            const now = Date.now();
            if (now - lastPush >= 1000) {
                lastPush = now;
                // Hold the power trend in debug mode so detector volts don't leave a fake dip.
                if (!dbg) push(hPwr, fwd);
                push(hWat, wt); push(hPlt, pt);
                drawTrends();
            }
        }

        let lastOk = 0;
        function updateStatus() {
            fetch('/status').then(r => r.ok ? r.json() : null)
            .then(d => { if (d) { lastOk = Date.now(); render(d); } })
            .catch(() => {});
        }
        // /status answers 503 while a UART exchange is pending, so only a sustained gap
        // means the controller is actually unreachable.
        function updateLink() {
            const up = Date.now() - lastOk < 2500;
            setText('linkChip', up ? 'LINK' : 'NO LINK', 'chip ' + (up ? 'ok' : 'bad'));
        }

        ['gPwr', 'gSwr', 'gWat', 'gPlt'].forEach(gauge);
        $('stateToggle').addEventListener('change', function() {
            const box = this;
            fetch('/setstate', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
                body:'state=' + box.checked })
            .then(r => { if (!r.ok) throw 0; })
            .catch(() => { box.checked = !box.checked; });   // refused (busy/TX): don't show a state we're not in
        });
        loadLimits(1);
        // No immediate updateLink(): lastOk is still 0 until the first /status reply
        // lands, so it would flash NO LINK on every page load.
        updateStatus();
        setInterval(updateStatus, 500);
        setInterval(updateLink, 1000);
    </script>
    )rawliteral");
    server.sendContent_P(ALERT_HTML);
    server.sendContent_P("</body></html>");
    server.sendContent("");   // terminate chunked response
}

// ─── /status (JSON) ───────────────────────────────────────────────────────────

void handleStatus() {
    if (waitingForResponse != RESPONSE_NONE) {
        server.send(503, "application/json", "{\"error\":\"Busy\"}");
        return;
    }
    // The six ADC-fed fields go out with 4 decimals unconditionally: in debug mode they
    // carry raw volts, where one backend ADC LSB is ~0.8 mV and %.2f would round real
    // bits away. The page formats them for display (2 dp for watts, 1 dp for V/A), so
    // the normal-mode readout is unchanged.
    char r[512];
    snprintf(r, sizeof(r),
        "{\"fwd\":%.4f,\"ref\":%.4f,\"trxfwd\":%.4f,\"swr\":%.2f,"
        "\"voltage\":%.4f,\"current\":%.4f,\"rsrv\":%.4f,"
        "\"water_temp\":%.1f,\"plate_temp\":%.1f,\"coeff\":%.1f,"
        "\"ptt\":%s,\"state\":%s,\"alarm\":%s,\"pwm_pump\":%d,\"pwm_cooler\":%d,"
        "\"protection_enabled\":%s,\"debug\":%s,\"alert_reason\":\"%s\",\"band\":\"%s\"}",
        status.fwd, status.ref, status.trxfwd, status.swr,
        status.voltage, status.current, status.rsrv,
        status.water_temp, status.plate_temp, status.coeff,
        status.ptt ? "true" : "false", status.state ? "true" : "false",
        status.alarm ? "true" : "false", status.pwm_pump, status.pwm_cooler,
        status.protection_enabled ? "true" : "false",
        status.debug ? "true" : "false",
        status.alert_reason, status.band);
    server.send(200, "application/json", r);
}

// ─── /band ────────────────────────────────────────────────────────────────────

void handleBandPage() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", pageHead("Band", "/band"));
    server.sendContent_P(R"rawliteral(
    <main class="wrap">
        <h1>Band</h1>
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
        <div id="message" class="message"></div>
    </main>
    <script>
    )rawliteral");
    server.sendContent_P(ALERT_JS);
    server.sendContent_P(R"rawliteral(
        function setBand(band) {
            const msg = document.getElementById('message');
            fetch('/setband', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'band='+band })
            .then(r => {
                if (!r.ok) throw 0;
                msg.className = 'message'; msg.textContent = 'Band set to ' + band;
                setTimeout(() => window.location.href='/', 50);
            })
            .catch(() => { msg.className = 'message err'; msg.textContent = 'Not changed: backend busy or transmitting'; });
        }
        document.addEventListener('DOMContentLoaded', () => {
            fetch('/status').then(r => r.json()).then(data => {
                document.querySelectorAll('.band-btn').forEach(btn =>
                    btn.classList.toggle('on', btn.textContent.trim() === data.band));
            }).catch(console.error);
        });
    </script>
    )rawliteral");
    server.sendContent_P(ALERT_HTML);
    server.sendContent_P("</body></html>");
    server.sendContent("");   // terminate chunked response
}

// ─── /setband ─────────────────────────────────────────────────────────────────

void handleSetBand() {
    if (!server.hasArg("band")) { server.send(400, "text/plain", "Missing band parameter"); return; }
    if (backendBusy() || isTransmitting()) { server.send(409, "text/plain", "Backend busy or transmitting"); return; }
    String band = server.arg("band");
    memset(state.band,  0, sizeof(state.band));
    memset(status.band, 0, sizeof(status.band));
    strncpy(state.band,  band.c_str(), sizeof(state.band)  - 1);
    strncpy(status.band, band.c_str(), sizeof(status.band) - 1);
    LOG_INFO("Band set to %s via web", state.band);
    sendStateData();
    server.send(200, "text/plain", "OK");
}

// ─── /setstate ────────────────────────────────────────────────────────────────

void handleSetState() {
    if (!server.hasArg("state")) { server.send(400, "text/plain", "Missing state parameter"); return; }
    if (backendBusy() || isTransmitting()) { server.send(409, "text/plain", "Backend busy or transmitting"); return; }
    state.state = (server.arg("state") == "true");
    LOG_INFO("Amplifier set to %s via web", state.state ? "ON" : "OFF");
    sendStateData();
    server.send(200, "text/plain", "OK");
}

// ─── /setdebug ────────────────────────────────────────────────────────────────

void handleSetDebug() {
    if (!server.hasArg("debug")) { server.send(400, "text/plain", "Missing debug parameter"); return; }
    // Gated on backendBusy() only, deliberately NOT on isTransmitting(). Debug is a live
    // state flag, not a stored setting: the backend keeps every protection check running
    // on the real computed values and only changes what it *reports*, so there is no
    // FLASH write and nothing to refuse while keyed. Watching the detector voltages under
    // TX is precisely what the mode exists for.
    if (backendBusy()) { server.send(409, "text/plain", "Backend busy"); return; }
    status.debug = (server.arg("debug") == "true");
    LOG_INFO("Debug mode set to %s via web", status.debug ? "ON" : "OFF");
    // sendDebugState(), not sendStateData(): pushes the single flag without re-asserting
    // enabled/ptt/band, so ticking it mid-transmission cannot unkey the amplifier. If the
    // backend misses it entirely, the next status frame overwrites status.debug with the
    // backend's real value, so the checkbox cannot keep claiming a mode that is not on.
    sendDebugState();
    server.send(200, "text/plain", "OK");
}

// ─── /settings ────────────────────────────────────────────────────────────────

void handleSettingsPage() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", pageHead("Settings", "/settings"));
    server.sendContent_P(R"rawliteral(
    <main class="wrap">
        <h1>Settings</h1>
        <form id="settingsForm" action="/savesettings" method="POST">
            <div class="form-grid">
                <section class="card"><h2>Protection</h2>
                    <div class="fields">
                        <div class="settings-item"><label>Max SWR</label>
                            <input type="number" id="max_swr" name="max_swr" step="1" min="1" max="10" value="0"></div>
                        <div class="settings-item"><label>Max current, A</label>
                            <input type="number" id="max_current" name="max_current" step="1" min="1" max="50" value="0"></div>
                        <div class="settings-item"><label>Max voltage, V</label>
                            <input type="number" id="max_voltage" name="max_voltage" step="1" min="1" max="60" value="0"></div>
                        <div class="settings-item"><label>Max input power, W</label>
                            <input type="number" id="max_input_power" name="max_input_power" step="1" min="1" max="10" value="0"></div>
                        <div class="settings-item"><label>Min efficiency, %</label>
                            <input type="number" id="min_coeff" name="min_coeff" step="1" min="30" max="90" value="0"></div>
                        <div class="settings-item"><label>Max plate temp, &deg;C</label>
                            <input type="number" id="max_plate_temp" name="max_plate_temp" step="1" min="10" max="100" value="0"></div>
                        <div class="settings-item"><label>Max water temp, &deg;C</label>
                            <input type="number" id="max_water_temp" name="max_water_temp" step="1" min="10" max="100" value="0"></div>
                    </div>
                    <div class="settings-item row"><label>Protection enabled</label>
                        <div class="toggle-switch">
                            <input type="checkbox" id="protection_enabled" name="protection_enabled" class="toggle-input">
                            <label for="protection_enabled" class="toggle-label"></label>
                        </div>
                    </div>
                </section>
                <section class="card"><h2>Cooling curves</h2>
                    <p class="hint">PWM ramps from minimum at the lower temperature to full at the upper one.</p>
                    <div class="fields">
                        <div class="settings-item"><label>Pump min speed at, &deg;C</label>
                            <input type="number" id="min_pump_speed_temp" name="min_pump_speed_temp" step="1" min="10" max="100" value="0"></div>
                        <div class="settings-item"><label>Pump max speed at, &deg;C</label>
                            <input type="number" id="max_pump_speed_temp" name="max_pump_speed_temp" step="1" min="10" max="100" value="0"></div>
                        <div class="settings-item"><label>Fan min speed at, &deg;C</label>
                            <input type="number" id="min_fan_speed_temp" name="min_fan_speed_temp" step="1" min="10" max="100" value="0"></div>
                        <div class="settings-item"><label>Fan max speed at, &deg;C</label>
                            <input type="number" id="max_fan_speed_temp" name="max_fan_speed_temp" step="1" min="10" max="100" value="0"></div>
                    </div>
                </section>
                <section class="card"><h2>Band</h2>
                    <div class="settings-item"><label>Default band</label>
                        <select id="default_band" name="default_band">
                            <option value="160m">160m</option><option value="80m">80m</option>
                            <option value="60m">60m</option><option value="40m">40m</option>
                            <option value="30m">30m</option><option value="20m">20m</option>
                            <option value="17m">17m</option><option value="15m">15m</option>
                            <option value="12m">12m</option><option value="10m">10m</option>
                            <option value="6m">6m</option>
                        </select>
                    </div>
                    <div class="settings-item row"><label>Auto band</label>
                        <div class="toggle-switch">
                            <input type="checkbox" id="autoband" name="autoband" class="toggle-input">
                            <label for="autoband" class="toggle-label"></label>
                        </div>
                    </div>
                </section>
                <section class="card"><h2>Diagnostics</h2>
                    <div class="settings-item row"><label>Debug (raw ADC voltages)</label>
                        <div class="toggle-switch">
                            <input type="checkbox" id="debug" class="toggle-input">
                            <label for="debug" class="toggle-label"></label>
                        </div>
                    </div>
                    <small id="debugMsg">Applies immediately. Status page then shows raw ADC input voltages instead of power/current/voltage. Protections keep running on the real values.</small>
                </section>
            </div>
            <div class="actions">
                <button type="submit" class="btn primary">Save settings</button>
                <span id="message" class="message"></span>
            </div>
        </form>
    </main>
    <script>
    )rawliteral");
    server.sendContent_P(ALERT_JS);
    server.sendContent_P(R"rawliteral(
        function loadSettings(attempt) {
            attempt = attempt || 1;
            const msg = document.getElementById('message');
            msg.style.color = 'var(--ok)';
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
                document.getElementById('debug').checked = data.debug;
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
                    msg.style.color = 'var(--bad)';
                    msg.textContent = 'Load failed. ';
                    const btn = document.createElement('button');
                    btn.type = 'button';
                    btn.textContent = 'Retry';
                    btn.onclick = () => { msg.style.color = 'var(--ok)'; msg.innerHTML = ''; loadSettings(1); };
                    msg.appendChild(btn);
                }
            });
        }
        // Debug is not part of the settings form — it is pushed the moment it is ticked
        // (see the no-name checkbox above). On failure the tick is reverted so the box
        // never claims a mode the backend is not actually in.
        document.getElementById('debug').addEventListener('change', function() {
            const box = this;
            const msg = document.getElementById('debugMsg');
            fetch('/setdebug', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
                body:'debug=' + box.checked })
            .then(r => {
                if (!r.ok) throw new Error('HTTP ' + r.status);
                msg.style.color = 'var(--ok)';
                msg.textContent = box.checked ? 'Debug ON - status page shows raw ADC voltages.'
                                              : 'Debug OFF - normal readings.';
            })
            .catch(() => {
                box.checked = !box.checked;
                msg.style.color = 'var(--bad)';
                msg.textContent = 'Backend busy, try again.';
            });
        });
        document.addEventListener('DOMContentLoaded', () => loadSettings(1));
        document.getElementById('settingsForm').addEventListener('submit', () => {
            document.getElementById('message').style.color = 'var(--ok)';
            document.getElementById('message').textContent = 'Saving...';
        });
    </script>
    )rawliteral");
    server.sendContent_P(ALERT_HTML);
    server.sendContent_P("</body></html>");
    server.sendContent("");   // terminate chunked response
}

void handleGetSettings() {
    requestAndWaitForSettings(300);
    if (waitingForResponse != RESPONSE_NONE) {
        server.send(503, "application/json", "{\"error\":\"Busy\"}");
        return;
    }
    // Build into a stack buffer instead of a growing String with a dozen
    // temporaries — no heap churn, same as the UART send path.
    char r[512];
    snprintf(r, sizeof(r),
        "{\"max_swr\":%d,\"max_current\":%d,\"max_voltage\":%d,"
        "\"max_plate_temp\":%d,\"max_water_temp\":%d,"
        "\"max_pump_speed_temp\":%d,\"min_pump_speed_temp\":%d,"
        "\"max_fan_speed_temp\":%d,\"min_fan_speed_temp\":%d,"
        "\"min_coeff\":%d,\"max_input_power\":%d,"
        "\"protection_enabled\":%s,\"autoband\":%s,\"default_band\":\"%s\","
        "\"debug\":%s}",
        settings.max_swr, settings.max_current, settings.max_voltage,
        settings.max_plate_temp, settings.max_water_temp,
        settings.max_pump_speed_temp, settings.min_pump_speed_temp,
        settings.max_fan_speed_temp, settings.min_fan_speed_temp,
        settings.min_coeff, settings.max_input_power,
        status.protection_enabled ? "true" : "false",
        settings.autoband ? "true" : "false",
        settings.default_band,
        status.debug ? "true" : "false");
    server.send(200, "application/json", r);
}

void handleSaveSettings() {
    if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
    if (backendBusy() || isTransmitting()) { server.send(409, "text/plain", "Backend busy or transmitting"); return; }
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

    sendStateData(false);   // fire-and-forget; the settings send below is tracked
    sendSettingsData();
    server.sendHeader("Location", "/"); server.send(303);
}

// ─── /calibration ─────────────────────────────────────────────────────────────

void handleCalibrationPage() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", pageHead("Calibration", "/calibration"));
    server.sendContent_P(R"rawliteral(
    <main class="wrap">
        <h1>Calibration</h1>
        <form id="calibrationForm" action="/savecalibration" method="POST">
            <div class="form-grid">
                <section class="card"><h2>Low band</h2>
                    <div class="fields">
                        <div class="settings-item"><label>FWD</label>
                            <input type="number" id="low_fwd_coeff" name="low_fwd_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                        <div class="settings-item"><label>REV</label>
                            <input type="number" id="low_rev_coeff" name="low_rev_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                        <div class="settings-item"><label>Input FWD</label>
                            <input type="number" id="low_ifwd_coeff" name="low_ifwd_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                    </div>
                </section>
                <section class="card"><h2>Mid band</h2>
                    <div class="fields">
                        <div class="settings-item"><label>FWD</label>
                            <input type="number" id="mid_fwd_coeff" name="mid_fwd_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                        <div class="settings-item"><label>REV</label>
                            <input type="number" id="mid_rev_coeff" name="mid_rev_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                        <div class="settings-item"><label>Input FWD</label>
                            <input type="number" id="mid_ifwd_coeff" name="mid_ifwd_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                    </div>
                </section>
                <section class="card"><h2>High band</h2>
                    <div class="fields">
                        <div class="settings-item"><label>FWD</label>
                            <input type="number" id="high_fwd_coeff" name="high_fwd_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                        <div class="settings-item"><label>REV</label>
                            <input type="number" id="high_rev_coeff" name="high_rev_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                        <div class="settings-item"><label>Input FWD</label>
                            <input type="number" id="high_ifwd_coeff" name="high_ifwd_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                    </div>
                </section>
                <section class="card"><h2>Supply &amp; sensors</h2>
                    <div class="fields">
                        <div class="settings-item"><label>Voltage coeff.</label>
                            <input type="number" id="voltage_coeff" name="voltage_coeff" step="0.0001" min="1" max="100" value="0"></div>
                        <div class="settings-item"><label>Current coeff.</label>
                            <input type="number" id="current_coeff" name="current_coeff" step="0.0001" min="0.1" max="100" value="0"></div>
                        <div class="settings-item"><label>Current sensor zero, V</label>
                            <input type="number" id="acs_zero" name="acs_zero" step="0.0001" min="0" max="10" value="0"></div>
                        <div class="settings-item"><label>Current sensor sens.</label>
                            <input type="number" id="acs_sens" name="acs_sens" step="0.0001" min="0" max="10" value="0"></div>
                        <div class="settings-item"><label>Reserve coeff.</label>
                            <input type="number" id="rsrv_coeff" name="rsrv_coeff" step="0.0001" min="1" max="1000" value="0"></div>
                    </div>
                </section>
            </div>
            <div class="actions">
                <button type="submit" class="btn primary">Save calibration</button>
                <span id="message" class="message"></span>
            </div>
        </form>
    </main>
    <script>
    )rawliteral");
    server.sendContent_P(ALERT_JS);
    server.sendContent_P(R"rawliteral(
        function loadCalibration(attempt) {
            attempt = attempt || 1;
            const msg = document.getElementById('message');
            msg.style.color = 'var(--ok)';
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
                    msg.style.color = 'var(--bad)';
                    msg.textContent = 'Load failed. ';
                    const btn = document.createElement('button');
                    btn.type = 'button';
                    btn.textContent = 'Retry';
                    btn.onclick = () => { msg.style.color = 'var(--ok)'; msg.innerHTML = ''; loadCalibration(1); };
                    msg.appendChild(btn);
                }
            });
        }
        document.addEventListener('DOMContentLoaded', () => loadCalibration(1));
        document.getElementById('calibrationForm').addEventListener('submit', () => {
            document.getElementById('message').style.color = 'var(--ok)';
            document.getElementById('message').textContent = 'Saving calibration...';
        });
    </script>
    )rawliteral");
    server.sendContent_P(ALERT_HTML);
    server.sendContent_P("</body></html>");
    server.sendContent("");   // terminate chunked response
}

void handleGetCalibration() {
    if (!requestAndWaitForCalibration(300)) {
        server.send(503, "application/json", "{\"error\":\"Busy or timeout\"}");
        return;
    }
    char r[640];
    snprintf(r, sizeof(r),
        "{\"low_fwd_coeff\":%.4f,\"low_rev_coeff\":%.4f,\"low_ifwd_coeff\":%.4f,"
        "\"mid_fwd_coeff\":%.4f,\"mid_rev_coeff\":%.4f,\"mid_ifwd_coeff\":%.4f,"
        "\"high_fwd_coeff\":%.4f,\"high_rev_coeff\":%.4f,\"high_ifwd_coeff\":%.4f,"
        "\"voltage_coeff\":%.4f,\"current_coeff\":%.4f,\"rsrv_coeff\":%.4f,"
        "\"acs_zero\":%.4f,\"acs_sens\":%.4f}",
        calibration.low_fwd_coeff,  calibration.low_rev_coeff,  calibration.low_ifwd_coeff,
        calibration.mid_fwd_coeff,  calibration.mid_rev_coeff,  calibration.mid_ifwd_coeff,
        calibration.high_fwd_coeff, calibration.high_rev_coeff, calibration.high_ifwd_coeff,
        calibration.voltage_coeff,  calibration.current_coeff,  calibration.rsrv_coeff,
        calibration.acs_zero,       calibration.acs_sens);
    server.send(200, "application/json", r);
}

void handleSaveCalibration() {
    if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
    if (backendBusy() || isTransmitting()) { server.send(409, "text/plain", "Backend busy or transmitting"); return; }
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
    static const char css[] PROGMEM = R"css(
:root { --bg:#0e1115; --card:#171b21; --card2:#20262e; --line:#2a313b; --txt:#e7eaef; --mut:#8b95a4;
        --acc:#3d8bfd; --ok:#2fbf71; --warn:#f0a020; --bad:#ef4444; --tx:#ff3b3b; --wat:#22c3ee; --plt:#fb923c; }
* { box-sizing: border-box; }
[hidden] { display: none !important; }
body { margin: 0; background: var(--bg); color: var(--txt); font: 15px/1.4 system-ui, -apple-system, "Segoe UI", Roboto, Arial, sans-serif; }
.top { display: flex; align-items: center; gap: 20px; padding: 10px 16px; background: var(--card); border-bottom: 1px solid var(--line); position: sticky; top: 0; z-index: 10; flex-wrap: wrap; }
.brand { font-weight: 800; letter-spacing: .1em; }
.brand span { color: var(--acc); }
.top nav { display: flex; gap: 4px; flex-wrap: wrap; }
.top nav a { color: var(--mut); text-decoration: none; padding: 6px 12px; border-radius: 6px; }
.top nav a:hover { color: var(--txt); background: var(--card2); }
.top nav a.on { color: var(--txt); background: var(--card2); box-shadow: inset 0 -2px var(--acc); }
body.ptt-on .top { box-shadow: inset 0 -3px var(--tx); }
.wrap { max-width: 1100px; margin: 0 auto; padding: 16px; }
h1 { font-size: 1.3em; margin: 4px 0 16px; }
.card { background: var(--card); border: 1px solid var(--line); border-radius: 12px; padding: 14px 16px; }
.card h2 { margin: 0 0 10px; font-size: .75em; letter-spacing: .12em; text-transform: uppercase; color: var(--mut); font-weight: 600; }
.card h2 small { float: right; text-transform: none; letter-spacing: 0; font-weight: 400; }
/* status strip */
.statusbar { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-bottom: 14px; }
.chip { padding: 5px 12px; border-radius: 999px; background: var(--card2); border: 1px solid var(--line); color: var(--mut); font-size: .85em; font-weight: 600; letter-spacing: .04em; }
.chip b { color: var(--txt); }
.chip.ok  { color: var(--ok);  border-color: rgba(47,191,113,.4); }
.chip.tx  { color: #fff; background: var(--tx); border-color: var(--tx); }
.chip.bad { color: #fff; background: var(--bad); border-color: var(--bad); }
.amp-switch { margin-left: auto; display: flex; align-items: center; gap: 10px; font-weight: 600; color: var(--mut); }
.debug-banner { background: rgba(240,160,32,.12); color: var(--warn); border: 1px solid rgba(240,160,32,.4); border-radius: 10px; padding: 10px 14px; margin-bottom: 14px; font-weight: 600; text-align: center; }
/* gauges */
.gauges { display: grid; grid-template-columns: repeat(4, 1fr); gap: 12px; margin-bottom: 12px; }
.gauge { text-align: center; padding: 12px 12px 10px; }
.gauge svg { width: 100%; display: block; }
.g-track { fill: none; stroke: var(--card2); stroke-width: 10; }
.g-val { fill: none; stroke: var(--ok); stroke-width: 10; transition: stroke-dasharray .4s ease, stroke .3s; }
.g-val.acc { stroke: var(--acc); }
.g-val.tx { stroke: var(--tx); }
.g-val.warn { stroke: var(--warn); }
.g-val.bad { stroke: var(--bad); }
.g-lim { stroke: var(--txt); stroke-width: 2; opacity: .75; }
.gauge text { fill: var(--txt); text-anchor: middle; font-variant-numeric: tabular-nums; }
.g-num { font-size: 18px; font-weight: 700; }
.g-unit, .g-sc { fill: var(--mut) !important; font-size: 8px; }
.g-sc { font-size: 7px; }
.g-title { color: var(--mut); font-size: .75em; text-transform: uppercase; letter-spacing: .12em; margin-top: 4px; }
body.ptt-on #gPwr { border-color: var(--tx); }
/* stat cards */
.cards { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; }
.wide { grid-column: 1 / -1; }
.stat { display: flex; justify-content: space-between; align-items: baseline; padding: 5px 0; }
.stat span { color: var(--mut); }
.stat b { font-size: 1.1em; font-variant-numeric: tabular-nums; }
.stat b.warn { color: var(--warn); }
.stat b.bad { color: var(--bad); }
body.debug-on .stat b { font-family: ui-monospace, Menlo, monospace; }
.meter { height: 6px; background: var(--card2); border-radius: 3px; overflow: hidden; margin: 2px 0 8px; }
.meter i { display: block; height: 100%; width: 0; background: var(--acc); transition: width .4s ease; }
.meter i.ok { background: var(--ok); }
.meter i.warn { background: var(--warn); }
.meter i.bad { background: var(--bad); }
/* trends */
.trends { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
.t-head { display: flex; justify-content: space-between; font-size: .85em; color: var(--mut); margin-bottom: 6px; }
.t-head b { font-weight: 500; font-variant-numeric: tabular-nums; }
.trends svg { width: 100%; height: 90px; background: var(--card2); border-radius: 8px; display: block; }
.trends polyline { fill: none; stroke-width: 2; vector-effect: non-scaling-stroke; stroke-linejoin: round; }
.t-pwr { stroke: var(--acc); } .l-pwr { color: var(--acc); }
.t-wat { stroke: var(--wat); } .l-wat { color: var(--wat); }
.t-plt { stroke: var(--plt); } .l-plt { color: var(--plt); }
/* forms */
.form-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 12px; }
.fields { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 10px 12px; }
.settings-item { display: flex; flex-direction: column; gap: 5px; margin-bottom: 4px; }
.settings-item.row { flex-direction: row; justify-content: space-between; align-items: center; margin-top: 12px; }
.settings-item label { color: var(--mut); font-size: .85em; }
.settings-item input, .settings-item select { width: 100%; padding: 8px 10px; background: var(--card2); color: var(--txt); border: 1px solid var(--line); border-radius: 8px; font-size: 1em; font-variant-numeric: tabular-nums; }
.settings-item input:focus, .settings-item select:focus { outline: none; border-color: var(--acc); box-shadow: 0 0 0 2px rgba(61,139,253,.25); }
.hint, small { color: var(--mut); font-size: .85em; }
.hint { margin: -4px 0 10px; }
.actions { display: flex; gap: 14px; align-items: center; margin-top: 16px; flex-wrap: wrap; }
button { font: inherit; }
.btn, .message button { padding: 10px 22px; border: 1px solid var(--line); background: var(--card2); color: var(--txt); border-radius: 8px; cursor: pointer; font-weight: 600; }
.btn.primary { background: var(--acc); border-color: var(--acc); color: #fff; }
.btn:hover, .message button:hover { filter: brightness(1.15); }
.message { color: var(--ok); font-weight: 600; min-height: 20px; }
.message.err { color: var(--bad); }
/* toggle */
.toggle-switch { display: flex; align-items: center; }
.toggle-input { display: none; }
.toggle-label { position: relative; display: inline-block; width: 46px; height: 24px; background: var(--line); border-radius: 24px; cursor: pointer; transition: background-color .25s; }
.toggle-label:before { content: ""; position: absolute; width: 20px; height: 20px; border-radius: 50%; background: #fff; top: 2px; left: 2px; transition: transform .25s; }
.toggle-input:checked + .toggle-label { background: var(--ok); }
.toggle-input:checked + .toggle-label:before { transform: translateX(22px); }
/* band */
.band-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(120px, 1fr)); gap: 10px; margin-bottom: 14px; }
.band-btn { padding: 22px 10px; font-size: 1.25em; font-weight: 700; background: var(--card); color: var(--txt); border: 1px solid var(--line); border-radius: 12px; cursor: pointer; transition: border-color .2s, background .2s; }
.band-btn:hover { border-color: var(--acc); }
.band-btn.on { background: #b91c1c; border-color: var(--bad); box-shadow: 0 0 14px rgba(239,68,68,.45); }
/* alert modal */
.alert-overlay { position: fixed; inset: 0; background: rgba(0,0,0,.75); display: none; justify-content: center; align-items: center; z-index: 1000; }
.alert-modal { background: var(--card); padding: 30px; border-radius: 15px; box-shadow: 0 10px 40px rgba(0,0,0,.5); max-width: 400px; width: 90%; text-align: center; border: 3px solid var(--bad); }
.alert-title { color: var(--bad); font-size: 24px; font-weight: bold; margin-bottom: 15px; letter-spacing: .1em; }
.alert-message { color: var(--txt); font-size: 17px; margin-bottom: 25px; line-height: 1.4; }
.alert-button { background: var(--bad); color: #fff; border: none; padding: 12px 30px; border-radius: 8px; font-size: 16px; font-weight: 600; cursor: pointer; }
.alert-button:hover { filter: brightness(1.15); }
.alert-visible { display: flex !important; }
@media (max-width: 860px) {
    .gauges { grid-template-columns: repeat(2, 1fr); }
    .cards { grid-template-columns: 1fr 1fr; }
}
@media (max-width: 560px) {
    .wrap { padding: 12px; }
    .top { gap: 8px; }
    .cards, .trends { grid-template-columns: 1fr; }
    .amp-switch { margin-left: 0; }
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
    server.on("/setdebug",       HTTP_POST, handleSetDebug);
    server.on("/style.css",      HTTP_GET,  handleCSS);
    server.on("/calibration",    HTTP_GET,  handleCalibrationPage);
    server.on("/getcalibration", HTTP_GET,  handleGetCalibration);
    server.on("/savecalibration",HTTP_POST, handleSaveCalibration);
    server.on("/resetalert",     HTTP_POST, handleResetAlert);
    server.on("/reseterror",     HTTP_POST, handleResetError);
    server.on("/events",         HTTP_GET,  handleEvents);
}
