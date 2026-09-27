// Common ESP32 firmware shell. Keep setup()/loop() and OTA logic here.
// Put device-specific logic in app.ino using app_setup() and app_loop().

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>

// For another repository under the same GitHub account, change only OTA_PROJECT_NAME.
// Keep OTA_DEVICE_MODEL the same for devices that share a firmware manifest.
static const char *OTA_PROJECT_NAME = "HMMD";
static const char *OTA_GITHUB_OWNER = "smarthomebyvinay";
static const char *OTA_DEVICE_MODEL = "esp32dev";
static const char *OTA_FIRMWARE_VERSION = "0.1.0";
static const char *SETUP_AP_PREFIX = "ESP32-Setup-";
static const char *SETUP_AP_PASSWORD = "configure32"; // At least 8 characters.

static String getManifestUrl() {
  return String("https://raw.githubusercontent.com/") + OTA_GITHUB_OWNER + "/" +
         OTA_PROJECT_NAME + "/main/firmware/manifest.json";
}

void app_setup();
void app_loop();

static WebServer server(80);
static Preferences preferences;
static String deviceId;
static String runningVersion;
static bool portalMode = false;
static bool uploadInProgress = false;
static bool uploadSucceeded = false;
static bool manifestInstalled = false;
static unsigned long lastManifestCheck = 0;
static const unsigned long MANIFEST_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;

static String htmlEscape(const String &value) {
  String out;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else out += c;
  }
  return out;
}

static String makePage(const String &notice = "") {
  String html = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#101b3f">
<title>ESP32 Device Setup</title><style>
:root{color-scheme:light;--ink:#16213e;--muted:#68738c;--line:#e5e9f2;--blue:#4263eb;--violet:#7950f2;--green:#16875b;--red:#c53d4b}
*{box-sizing:border-box}body{margin:0;min-height:100vh;background:linear-gradient(145deg,#f1f5ff,#f8f7ff 48%,#eefaf8);font:16px/1.5 Inter,ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif;color:var(--ink)}
.wrap{max-width:900px;margin:auto;padding:28px 18px 56px}.hero{padding:26px 28px;color:white;border-radius:22px;background:linear-gradient(120deg,#253c92,#6046c9 58%,#008b91);box-shadow:0 14px 35px #253c9226}
.heroTop{display:flex;align-items:flex-start;justify-content:space-between;gap:16px}.eyebrow{text-transform:uppercase;letter-spacing:.14em;font-size:.75rem;font-weight:800;opacity:.78}.hero h1{margin:5px 0;font-size:clamp(1.8rem,5vw,2.6rem);line-height:1.12}.hero p{margin:8px 0 0;opacity:.88}.badge{border:1px solid #ffffff65;border-radius:999px;padding:6px 12px;font-size:.82rem;white-space:nowrap;background:#ffffff18}
.meta{display:flex;flex-wrap:wrap;gap:9px;margin-top:20px}.chip{background:#ffffff1d;border:1px solid #ffffff30;padding:6px 11px;border-radius:999px;font-size:.84rem}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px;margin-top:18px}.card{background:#fff;border:1px solid var(--line);border-radius:18px;padding:22px;box-shadow:0 8px 24px #1c2a5010}.wide{grid-column:1/-1}.heading{display:flex;gap:12px;align-items:center;margin-bottom:14px}.icon{width:42px;height:42px;display:grid;place-items:center;border-radius:13px;background:#eef1ff;font-size:1.25rem}.heading h2{font-size:1.12rem;margin:0}.heading p{color:var(--muted);font-size:.9rem;margin:2px 0 0}
label{display:block;font-weight:650;font-size:.9rem;margin:12px 0 6px}input,select{font:inherit;color:var(--ink);width:100%;padding:12px 13px;border:1px solid #d7ddea;border-radius:11px;background:#fff;outline:none}input:focus,select:focus{border-color:var(--blue);box-shadow:0 0 0 3px #4263eb20}.row{display:flex;gap:9px;align-items:center}.row select{flex:1;min-width:0}.row button{width:auto;white-space:nowrap}
button{font:inherit;font-weight:700;color:#fff;border:0;border-radius:11px;padding:12px 16px;background:linear-gradient(100deg,var(--blue),var(--violet));cursor:pointer;transition:transform .15s,filter .15s}button:hover{filter:brightness(1.06);transform:translateY(-1px)}button:disabled{opacity:.56;cursor:wait;transform:none}.secondary{background:#eef1ff;color:#3349a5}.filebox{border:1px dashed #bbc5da;border-radius:12px;padding:12px;background:#fafbff}small,.hint{color:var(--muted);font-size:.84rem}.hint{margin:7px 0 0}.status{min-height:24px;margin:10px 0 0;color:var(--muted);font-size:.9rem}.status.good{color:var(--green);font-weight:700}.status.bad{color:var(--red);font-weight:700}
.progress{height:12px;background:#edf0f7;border-radius:99px;overflow:hidden;margin-top:12px}.bar{height:100%;width:0;background:linear-gradient(90deg,#4263eb,#7950f2,#11a58c);border-radius:99px;transition:width .2s}.progressMeta{display:flex;justify-content:space-between;gap:8px;margin-top:6px;color:var(--muted);font-size:.82rem}.hidden{display:none!important}.spinner{display:inline-block;width:14px;height:14px;border:2px solid #cbd2e2;border-top-color:var(--blue);border-radius:50%;animation:spin .8s linear infinite;vertical-align:-2px;margin-right:6px}@keyframes spin{to{transform:rotate(360deg)}}
.toast{position:fixed;left:50%;bottom:18px;transform:translate(-50%,20px);opacity:0;pointer-events:none;background:#17213a;color:white;padding:11px 17px;border-radius:12px;box-shadow:0 8px 28px #101b3f45;transition:.25s;max-width:calc(100% - 28px);z-index:3}.toast.show{opacity:1;transform:translate(-50%,0)}footer{text-align:center;color:var(--muted);font-size:.82rem;margin-top:18px}
@media(max-width:680px){.wrap{padding:14px 12px 36px}.hero{padding:21px 19px;border-radius:18px}.grid{grid-template-columns:1fr;gap:12px;margin-top:12px}.wide{grid-column:auto}.card{padding:18px}.heroTop{align-items:center}.badge{font-size:.75rem}.row{align-items:stretch}.row button{padding:10px}}
</style></head><body><main class="wrap">
<header class="hero"><div class="heroTop"><div><div class="eyebrow">Device control center</div><h1>ESP32 setup</h1><p>Configure your network or safely update firmware.</p></div><span class="badge">● <span id="connectionLabel">)HTML";
  html += WiFi.status() == WL_CONNECTED ? F("Online") : F("Setup mode");
  html += R"HTML(</span></span></div><div class="meta"><span class="chip">Device: <b>)HTML";
  html += htmlEscape(deviceId);
  html += R"HTML(</b></span><span class="chip">Firmware: <b>)HTML";
  html += htmlEscape(runningVersion);
  html += R"HTML(</b></span><span class="chip">Network: <b id="networkName">)HTML";
  html += WiFi.status() == WL_CONNECTED ? htmlEscape(WiFi.SSID()) : F("Setup access point");
  html += R"HTML(</b></span></div></header>
<div class="grid"><section class="card"><div class="heading"><span class="icon">📶</span><div><h2>Wi-Fi connection</h2><p>Choose a nearby network for this device.</p></div></div>
<form id="wifiForm"><label for="ssid">Available networks</label><div class="row"><select id="ssid" name="ssid"><option value="">Scanning for networks…</option></select><button type="button" class="secondary" id="scanButton">Scan</button></div><label for="manualSsid">Or enter network name</label><input id="manualSsid" maxlength="32" placeholder="Wi-Fi name (optional)">
<label for="password">Wi-Fi password</label><input id="password" name="password" type="password" maxlength="64" autocomplete="new-password" placeholder="Enter network password">
<button id="wifiButton" type="submit" style="width:100%;margin-top:14px">Save Wi-Fi and restart</button><p class="hint">After restart, reconnect to your router. The device IP appears in Serial Monitor.</p><p id="wifiStatus" class="status" aria-live="polite"></p></form></section>
<section class="card"><div class="heading"><span class="icon">⬆️</span><div><h2>Manual firmware update</h2><p>Install a compiled ESP32 application binary.</p></div></div>
<form id="uploadForm"><label for="firmware">Firmware file (.ino.bin)</label><div class="filebox"><input id="firmware" name="firmware" type="file" accept=".bin,application/octet-stream" required></div><p class="hint">Use the application .bin, not the bootloader or partition file.</p>
<button id="uploadButton" type="submit" style="width:100%;margin-top:14px">Upload and install</button></form>
<div id="uploadPanel" class="hidden" aria-live="polite"><p id="uploadStatus" class="status"></p><div class="progress"><div id="uploadBar" class="bar"></div></div><div class="progressMeta"><span id="progressStage">Ready</span><b id="progressPercent">0%</b></div></div></section>
<section class="card wide"><div class="heading"><span class="icon">🌐</span><div><h2>Online update</h2><p>Check the configured Git manifest for a newer firmware release.</p></div></div><button id="checkButton">Check for updates</button><p id="onlineStatus" class="status" aria-live="polite"></p></section></div>
<footer>Keep the device powered while firmware is being written.</footer></main><div id="toast" class="toast" role="status"></div>
<script>
const $=id=>document.getElementById(id);let toastTimer;
function toast(message){const t=$('toast');t.textContent=message;t.classList.add('show');clearTimeout(toastTimer);toastTimer=setTimeout(()=>t.classList.remove('show'),3000)}
function setStatus(el,text,kind=''){el.textContent=text;el.className='status'+(kind?' '+kind:'')}
async function scanNetworks(){const button=$('scanButton'),select=$('ssid');button.disabled=true;button.innerHTML='<span class="spinner"></span>Scanning';select.innerHTML='<option value="">Scanning for networks…</option>';try{const r=await fetch('/api/scan?t='+Date.now(),{cache:'no-store'});if(!r.ok)throw Error('Scan failed');const data=await r.json();select.innerHTML='';if(!data.networks||!data.networks.length){select.add(new Option('No networks found — type the name below',''));return}for(const n of data.networks){const bars=n.rssi>-60?'Strong':n.rssi>-75?'Good':'Weak';const security=n.secure?'🔒':'Open';select.add(new Option(`${n.ssid}  ·  ${bars} signal  ·  ${security}`,n.ssid))}}catch(e){select.innerHTML='<option value="">Scan unavailable — enter network name</option>';toast('Could not scan Wi-Fi networks')}finally{button.disabled=false;button.textContent='Scan'}}
$('scanButton').addEventListener('click',scanNetworks);
$('wifiForm').addEventListener('submit',async e=>{e.preventDefault();const b=$('wifiButton');b.disabled=true;setStatus($('wifiStatus'),'Saving Wi-Fi settings and restarting…');try{const params=new URLSearchParams(new FormData(e.currentTarget));if($('manualSsid').value.trim())params.set('ssid',$('manualSsid').value.trim());const r=await fetch('/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params});if(!r.ok)throw Error('Could not save credentials');setStatus($('wifiStatus'),'Credentials saved. The device is restarting and will join the selected network.','good');toast('Wi-Fi settings saved')}catch(err){setStatus($('wifiStatus'),'Could not save Wi-Fi settings. Please try again.','bad');b.disabled=false}});
function showProgress(percent,stage,message){$('uploadPanel').classList.remove('hidden');$('uploadBar').style.width=Math.max(0,Math.min(100,percent))+'%';$('progressPercent').textContent=Math.round(percent)+'%';$('progressStage').textContent=stage;if(message)setStatus($('uploadStatus'),message)}
async function waitForReconnect(){showProgress(100,'Restarting','Firmware installed. Waiting for the device to restart and reconnect…');const start=Date.now();while(Date.now()-start<90000){await new Promise(r=>setTimeout(r,1800));try{const res=await fetch('/api/status?t='+Date.now(),{cache:'no-store',signal:AbortSignal.timeout(2500)});if(res.ok){const d=await res.json();$('networkName').textContent=d.ssid||'Setup access point';$('connectionLabel').textContent=d.connected?'Online':'Setup mode';$('uploadBar').style.width='100%';$('progressStage').textContent='Complete';setStatus($('uploadStatus'),'Device is back online'+(d.version?' · firmware '+d.version:'')+'.','good');toast('Firmware installed and device reconnected');$('uploadButton').disabled=false;$('checkButton').disabled=false;$('checkButton').textContent='Check for updates';return}}catch(e){}}$('progressStage').textContent='Reconnect timeout';setStatus($('uploadStatus'),'The device has not responded yet. Check its power and Wi-Fi, then reload this page.','bad');$('uploadButton').disabled=false;$('checkButton').disabled=false;$('checkButton').textContent='Check for updates'}
$('uploadForm').addEventListener('submit',e=>{e.preventDefault();const file=$('firmware').files[0];if(!file)return;const b=$('uploadButton');b.disabled=true;showProgress(0,'Starting','Preparing firmware upload…');const xhr=new XMLHttpRequest();xhr.open('POST','/update');xhr.upload.onprogress=e=>{if(e.lengthComputable){const p=e.loaded/e.total*100;showProgress(p,p>=100?'Verifying and installing':'Uploading and writing to flash',p>=100?'Upload complete. Verifying and installing firmware…':`Uploading firmware… ${Math.round(p)}%`)}};xhr.onload=()=>{if(xhr.status>=200&&xhr.status<300){waitForReconnect()}else{setStatus($('uploadStatus'),'Upload failed. The firmware was rejected or could not be written.','bad');$('progressStage').textContent='Failed';b.disabled=false}};xhr.onerror=()=>{showProgress(100,'Checking device','Connection ended. Checking whether the device installed and restarted…');waitForReconnect()};const data=new FormData();data.append('firmware',file,file.name);xhr.send(data)});
$('checkButton').addEventListener('click',async()=>{const b=$('checkButton');b.disabled=true;b.innerHTML='<span class="spinner"></span>Checking manifest';setStatus($('onlineStatus'),'Checking for a firmware update…');try{const r=await fetch('/check',{method:'POST'});const d=await r.json();setStatus($('onlineStatus'),d.message,d.ok?'good':'');if(d.restarting){$('uploadPanel').classList.remove('hidden');showProgress(100,'Restarting','Online update installed. Waiting for the device to restart and reconnect…');b.textContent='Installing update';waitForReconnect();return}}catch(e){setStatus($('onlineStatus'),'Could not check for updates. Confirm Wi-Fi and try again.','bad')}b.disabled=false;b.textContent='Check for updates'});
scanNetworks();
</script></body></html>)HTML";
  return html;
}

static void showPage(const String &notice = "") {
  server.send(200, "text/html; charset=utf-8", makePage(notice));
}

static void startSetupAP() {
  portalMode = true;
  // A failed Wi-Fi.begin() can leave the station association attempt pending.
  // Stop it before changing radio mode and starting the provisioning AP.
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  String apName = String(SETUP_AP_PREFIX) + deviceId.substring(deviceId.length() - 4);
  WiFi.softAP(apName.c_str(), SETUP_AP_PASSWORD);
  Serial.printf("Setup Wi-Fi: %s | password: %s | open http://%s\n",
                apName.c_str(), SETUP_AP_PASSWORD, WiFi.softAPIP().toString().c_str());
}

static bool connectSavedWiFi() {
  preferences.begin("network", true);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  preferences.end();
  if (!ssid.length()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.printf("Connecting to saved Wi-Fi: %s\n", ssid.c_str());
  const unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) delay(250);
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.printf("Connected. Open http://%s\n", WiFi.localIP().toString().c_str());
  return true;
}

static bool installFromUrl(const String &url, const String &expectedSha,
                           size_t expectedSize, String &error) {
  if (!url.startsWith("https://") || expectedSha.length() != 64 || expectedSize == 0) {
    error = "Manifest URL, size, or SHA-256 is invalid";
    return false;
  }
  HTTPClient http;
  http.setTimeout(30000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(url)) { error = "Could not open firmware URL"; return false; }
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    error = "Firmware server returned HTTP " + String(status);
    http.end();
    return false;
  }
  const int contentLength = http.getSize();
  if (contentLength <= 0 || (size_t)contentLength != expectedSize) {
    error = "Firmware size does not match manifest";
    http.end();
    return false;
  }
  if (!Update.begin((size_t)contentLength, U_FLASH)) {
    error = Update.errorString();
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  mbedtls_sha256_starts(&hash, 0);
  uint8_t buffer[1024];
  size_t received = 0;
  unsigned long lastData = millis();
  while (http.connected() && received < (size_t)contentLength) {
    const size_t available = stream->available();
    if (available) {
      const size_t count = stream->readBytes(buffer, min(available, sizeof(buffer)));
      if (!count || Update.write(buffer, count) != count) {
        error = Update.errorString();
        Update.abort();
        mbedtls_sha256_free(&hash);
        http.end();
        return false;
      }
      mbedtls_sha256_update(&hash, buffer, count);
      received += count;
      lastData = millis();
    } else if (millis() - lastData > 30000) {
      error = "Firmware download timed out";
      Update.abort();
      mbedtls_sha256_free(&hash);
      http.end();
      return false;
    } else {
      delay(2);
    }
  }

  uint8_t digest[32];
  char actualSha[65];
  mbedtls_sha256_finish(&hash, digest);
  mbedtls_sha256_free(&hash);
  for (size_t i = 0; i < sizeof(digest); ++i) sprintf(actualSha + i * 2, "%02x", digest[i]);
  actualSha[64] = '\0';
  http.end();
  String normalizedSha = expectedSha;
  normalizedSha.toLowerCase();
  if (received != (size_t)contentLength || normalizedSha != String(actualSha)) {
    error = "Firmware is incomplete or SHA-256 does not match";
    Update.abort();
    return false;
  }
  if (!Update.end(true)) { error = Update.errorString(); return false; }
  return true;
}

static bool checkManifest(String &message) {
  manifestInstalled = false;
  if (WiFi.status() != WL_CONNECTED) {
    message = "Connect the device to Wi-Fi before checking online updates";
    return false;
  }
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(getManifestUrl())) { message = "Could not open manifest URL"; return false; }
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    message = "Manifest server returned HTTP " + String(status);
    http.end();
    return false;
  }
  const int length = http.getSize();
  if (length <= 0 || length > 8192) {
    message = "Manifest size is invalid";
    http.end();
    return false;
  }
  JsonDocument manifest;
  const DeserializationError parseError = deserializeJson(manifest, http.getStream());
  http.end();
  if (parseError) { message = "Could not parse manifest JSON"; return false; }
  if (String((const char *)(manifest["device"] | "")) != OTA_DEVICE_MODEL) {
    message = "Manifest is for a different device model";
    return false;
  }
  const String version = manifest["version"] | "";
  const String url = manifest["url"] | "";
  String sha = manifest["sha256"] | "";
  const size_t size = manifest["size"] | (size_t)0;
  if (!version.length() || !url.length() || sha.length() != 64 || !size) {
    message = "Manifest needs version, url, sha256, and size";
    return false;
  }
  if (version == runningVersion) {
    message = "Firmware is already up to date (" + runningVersion + ")";
    return true;
  }
  String error;
  if (!installFromUrl(url, sha, size, error)) { message = "Update failed: " + error; return false; }
  preferences.begin("firmware", false);
  preferences.putString("version", version);
  preferences.end();
  manifestInstalled = true;
  message = "Installed firmware " + version + "; restarting";
  return true;
}

static void configureWebServer() {
  server.on("/", HTTP_GET, [] { showPage(); });
  server.on("/api/status", HTTP_GET, [] {
    JsonDocument status;
    status["connected"] = WiFi.status() == WL_CONNECTED;
    status["ssid"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("");
    status["version"] = runningVersion;
    status["model"] = OTA_DEVICE_MODEL;
    String body;
    serializeJson(status, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/scan", HTTP_GET, [] {
    WiFi.scanDelete();
    const int found = WiFi.scanNetworks(false, false);
    JsonDocument result;
    JsonArray networks = result["networks"].to<JsonArray>();
    for (int i = 0; i < found; ++i) {
      const String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;
      JsonObject network = networks.add<JsonObject>();
      network["ssid"] = ssid;
      network["rssi"] = WiFi.RSSI(i);
      network["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    String body;
    serializeJson(result, body);
    WiFi.scanDelete();
    server.send(200, "application/json", body);
  });
  server.on("/wifi", HTTP_POST, [] {
    const String ssid = server.arg("ssid");
    const String password = server.arg("password");
    if (!ssid.length() || ssid.length() > 32 || password.length() > 64) {
      server.send(400, "application/json", "{\"ok\":false,\"message\":\"Invalid Wi-Fi name or password length\"}");
      return;
    }
    preferences.begin("network", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();
    server.send(200, "application/json", "{\"ok\":true,\"message\":\"Wi-Fi settings saved\"}");
    delay(800);
    ESP.restart();
  });
  server.on("/check", HTTP_POST, [] {
    String message;
    const bool ok = checkManifest(message);
    JsonDocument result;
    result["ok"] = ok;
    result["message"] = message;
    result["restarting"] = manifestInstalled;
    String body;
    serializeJson(result, body);
    server.send(200, "application/json", body);
    if (manifestInstalled) { delay(1200); ESP.restart(); }
  });
  server.on("/update", HTTP_POST,
    [] {
      const bool success = uploadSucceeded && !Update.hasError();
      server.send(success ? 200 : 500, "application/json", success
          ? "{\"ok\":true,\"message\":\"Firmware installed\"}"
          : "{\"ok\":false,\"message\":\"Firmware upload failed\"}");
      if (success) { delay(1200); ESP.restart(); }
    },
    [] {
      HTTPUpload &upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        uploadSucceeded = false;
        uploadInProgress = upload.filename.endsWith(".bin") && Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
        if (!uploadInProgress) Update.printError(Serial);
      } else if (upload.status == UPLOAD_FILE_WRITE && uploadInProgress) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
          Update.abort();
          uploadInProgress = false;
        }
      } else if (upload.status == UPLOAD_FILE_END && uploadInProgress) {
        uploadSucceeded = Update.end(true);
        if (!uploadSucceeded) Update.printError(Serial);
        uploadInProgress = false;
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        uploadInProgress = false;
      }
    });
  server.onNotFound([] {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();
}

void setup() {
  Serial.begin(115200);
  const uint64_t mac = ESP.getEfuseMac();
  char id[13];
  snprintf(id, sizeof(id), "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
  deviceId = id;
  preferences.begin("firmware", true);
  runningVersion = preferences.getString("version", OTA_FIRMWARE_VERSION);
  preferences.end();

  if (!connectSavedWiFi()) startSetupAP();
  configureWebServer();
  app_setup();
  // Cause the first online manifest check as soon as setup has completed.
  lastManifestCheck = millis() - MANIFEST_INTERVAL_MS;
}

void loop() {
  server.handleClient();
  app_loop();
  if (WiFi.status() == WL_CONNECTED && millis() - lastManifestCheck >= MANIFEST_INTERVAL_MS) {
    lastManifestCheck = millis();
    String message;
    checkManifest(message);
    Serial.println(message);
    if (manifestInstalled) { delay(1200); ESP.restart(); }
  }
  delay(2);
}
