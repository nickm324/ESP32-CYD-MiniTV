#pragma once

#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <time.h>
#include <stdarg.h>

static WebServer webServer(80);
static DNSServer setupDnsServer;
static File uploadFile;
static bool uploadOK = false;
static bool uploadCurrentFileOK = false;
static bool uploadBatchActive = false;
static String uploadPath;
static size_t uploadChunkOffset = 0;
static size_t uploadStoredSize = 0;
static bool uploadIsChunked = false;
static bool uploadCancelRequested = false;
static volatile bool restartAfterResponse = false;
static uint32_t restartRequestedAt = 0;
static wl_status_t lastWiFiStatus = WL_IDLE_STATUS;
static String configuredSSID;
static String configuredPassword;
static String configuredDeviceName = "MiniTV";
static String configuredHostname = WIFI_HOSTNAME;
static String configuredAdminPassword;
static String configuredTimezone = "CST6CDT,M3.2.0,M11.1.0";
static bool dailyScheduleEnabled = false;
static String dailySleepTime = "23:00";
static String dailyWakeTime = "08:00";
static time_t lastScheduleMinute = 0;
static bool firmwareUpdateOK = false;
static String firmwareUpdateError;
static String restoreUploadJson;
static bool restoreUploadOK = false;
static String restoreUploadError;
static bool setupModeActive = false;
static String setupNetworkName;
static String setupNetworkPassword;

#define EVENT_LOG_CAPACITY 24
#define EVENT_LOG_MESSAGE_SIZE 112
static char eventLogMessages[EVENT_LOG_CAPACITY][EVENT_LOG_MESSAGE_SIZE];
static uint32_t eventLogTimes[EVENT_LOG_CAPACITY];
static uint8_t eventLogNext = 0;
static uint8_t eventLogCount = 0;
static portMUX_TYPE eventLogMux = portMUX_INITIALIZER_UNLOCKED;

static String htmlEscape(const String &value);
static String pageHeader(const char *title);
static void requestRestart();

static void logEvent(const char *format, ...) {
  char message[EVENT_LOG_MESSAGE_SIZE];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  portENTER_CRITICAL(&eventLogMux);
  eventLogTimes[eventLogNext] = millis();
  strncpy(eventLogMessages[eventLogNext], message, EVENT_LOG_MESSAGE_SIZE - 1);
  eventLogMessages[eventLogNext][EVENT_LOG_MESSAGE_SIZE - 1] = '\0';
  eventLogNext = (eventLogNext + 1) % EVENT_LOG_CAPACITY;
  if (eventLogCount < EVENT_LOG_CAPACITY) eventLogCount++;
  portEXIT_CRITICAL(&eventLogMux);
}

static bool adminAuthenticated() {
  return !configuredAdminPassword.length() ||
         webServer.authenticate("admin", configuredAdminPassword.c_str());
}

static bool requireAdmin() {
  if (adminAuthenticated()) return true;
  webServer.requestAuthentication(BASIC_AUTH, "MiniTV administration");
  return false;
}

static bool startWiFiSetupMode() {
  uint32_t chipSuffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X", chipSuffix);
  setupNetworkName = "MiniTV-Setup-" + String(suffix);
  setupNetworkPassword = "minitv-" + String(suffix);
  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  if (!WiFi.softAP(setupNetworkName.c_str(), setupNetworkPassword.c_str())) {
    Serial.println("ERROR: Could not start the MiniTV Wi-Fi setup network");
    return false;
  }
  setupModeActive = true;
  playback_paused = true;
  web_stop_requested = true;
  audio_stop_requested = true;
#ifdef AUDIO_ENABLE_PIN
  digitalWrite(AUDIO_ENABLE_PIN, HIGH);
#endif
  IPAddress setupIP = WiFi.softAPIP();
  setupDnsServer.start(53, "*", setupIP);
  Serial.printf("Wi-Fi setup network: %s\n", setupNetworkName.c_str());
  Serial.printf("Wi-Fi setup password: %s\n", setupNetworkPassword.c_str());
  Serial.printf("Open http://%s\n", setupIP.toString().c_str());
  logEvent("Wi-Fi setup mode started: %s", setupNetworkName.c_str());
  is_showing_message = true;
  gfx->fillScreen(BLACK);
  gfx->setTextWrap(false);
  gfx->setCursor(38, 38);
  gfx->setTextColor(CYAN);
  gfx->setTextSize(2);
  gfx->println("Wi-Fi setup");
  gfx->setTextSize(1);
  gfx->setTextColor(WHITE);
  gfx->setCursor(38, 72);
  gfx->println("Network:");
  gfx->setCursor(38, 86);
  gfx->println(setupNetworkName);
  gfx->setCursor(38, 108);
  gfx->println("Password:");
  gfx->setCursor(38, 122);
  gfx->println(setupNetworkPassword);
  gfx->setCursor(38, 148);
  gfx->println("Open in browser:");
  gfx->setCursor(38, 162);
  gfx->print("http://");
  gfx->println(setupIP.toString());
  return true;
}

static void sendWiFiSetupPage() {
  String page = pageHeader("MiniTV Wi-Fi Setup");
  page += "<h1>Connect " + htmlEscape(configuredDeviceName) + " to Wi-Fi</h1>";
  page += "<div class='card'><p>Select a local <strong>2.4 GHz</strong> network and enter its password. The MiniTV will save the details and restart.</p>";
  page += "<form method='POST' action='/wifi-setup-save'><label>Network</label><br><select name='ssid' required style='min-width:280px' onchange=\"document.getElementById('customSSID').style.display=this.value==='__manual__'?'inline-block':'none'\">";
  int networks = WiFi.scanNetworks(false, true);
  if (networks <= 0) {
    page += "<option value=''>No networks found—rescan the page</option>";
  } else {
    String seen = "\n";
    for (int i = 0; i < networks; i++) {
      String ssid = WiFi.SSID(i);
      if (!ssid.length() || seen.indexOf("\n" + ssid + "\n") >= 0) continue;
      seen += ssid + "\n";
      page += "<option value=\"" + htmlEscape(ssid) + "\">" + htmlEscape(ssid) + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
  }
  WiFi.scanDelete();
  page += F("<option value='__manual__'>Enter a hidden network manually...</option></select><input id='customSSID' type='text' name='custom_ssid' maxlength='32' placeholder='Hidden network name' style='display:none;min-width:280px'><br><label>Password</label><br><input type='password' name='password' maxlength='64' style='min-width:280px'><br>"
            "<button type='submit'>Save and connect</button></form><p class='meta'>The setup network shuts down automatically after a successful connection.</p></div></main></body></html>");
  webServer.send(200, "text/html", page);
}

static void handleWiFiSetupSave() {
  if (!setupModeActive) {
    webServer.send(409, "text/plain", "Wi-Fi setup mode is not active");
    return;
  }
  String ssid = webServer.arg("ssid");
  if (ssid == "__manual__") ssid = webServer.arg("custom_ssid");
  String password = webServer.arg("password");
  ssid.trim();
  if (!ssid.length() || ssid.length() > 32 || password.length() > 64) {
    webServer.send(400, "text/plain", "Enter a valid network name and password");
    return;
  }
  preferences.begin(APP_NAME, false);
  preferences.putString("wifi_ssid", ssid);
  preferences.putString("wifi_pass", password);
  preferences.end();
  String page = pageHeader("Wi-Fi saved");
  page += "<h1>Wi-Fi details saved</h1><div class='card'><p>The MiniTV is restarting and will try to connect to <strong>" + htmlEscape(ssid) + "</strong>.</p>";
  page += "<p>If it cannot connect, the setup network will return automatically.</p></div></main></body></html>";
  webServer.send(200, "text/html", page);
  requestRestart();
}

static void miniTVWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.printf("Wi-Fi disconnect reason: %d\n", (int)info.wifi_sta_disconnected.reason);
    logEvent("Wi-Fi disconnected, reason %d", (int)info.wifi_sta_disconnected.reason);
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.printf("Wi-Fi got IP: %s\n", WiFi.localIP().toString().c_str());
    logEvent("Wi-Fi connected: %s", WiFi.localIP().toString().c_str());
  }
}

static String htmlEscape(const String &value) {
  String out;
  out.reserve(value.length() + 16);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '\"') out += "&quot;";
    else out += c;
  }
  return out;
}

static String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '\\' || c == '"') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += "\\t";
    else if (c != '\r') out += c;
  }
  return out;
}

static String safeFilename(String name) {
  name.replace("\\", "/");
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  name.replace("..", "_");
  name.replace("\"", "_");
  name.replace("'", "_");
  return name;
}

static void requestRestart() {
  restartRequestedAt = millis();
  restartAfterResponse = true;
}

static String channelLabel(int channel) {
  if (channel == 0) return "Random";
  Preferences labels;
  labels.begin(APP_NAME, true);
  String value = labels.getString(("ch_" + String(channel)).c_str(), "");
  labels.end();
  return value.length() ? "Channel " + String(channel) + " · " + value : "Channel " + String(channel);
}

static String pageHeader(const char *title) {
  String s = F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
               "<style>*{box-sizing:border-box}body{font-family:Inter,system-ui,Arial,sans-serif;margin:0;background:radial-gradient(circle at top right,#172554,#0b1020 45%);color:#e5edf8;min-height:100vh}"
               ".sidebar{position:fixed;inset:0 auto 0 0;width:210px;padding:24px 14px;background:#090e1bee;border-right:1px solid #334155;backdrop-filter:blur(14px)}"
               ".brand{font-size:23px;font-weight:800;color:#67e8f9;margin:0 10px 24px}.navlink{display:block;padding:11px 13px;margin:5px 0;border-radius:9px;color:#cbd5e1;text-decoration:none}.navlink:hover{background:#1e3a5f;color:white}"
               ".content{max-width:980px;margin-left:210px;padding:24px 28px}h1{color:#67e8f9}h2{color:#a5b4fc;margin-top:0}"
               ".card{background:#162033e8;padding:18px;margin:16px 0;border:1px solid #334155;border-radius:16px;box-shadow:0 12px 32px #0005}"
               ".row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}button,input,select{font-size:16px;padding:10px;margin:4px;border-radius:8px;border:1px solid #475569}"
               "button{cursor:pointer;background:#2563eb;color:white;font-weight:650}button:hover{background:#3b82f6}.danger{background:#b91c1c}"
               "input,select{background:#0f172a;color:#eee}a{color:#67e8f9}progress{width:100%;height:20px;accent-color:#22c55e}"
               ".status{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px}.stat{background:#0f172a;padding:10px;border-radius:8px}"
               ".label{font-size:12px;color:#94a3b8;text-transform:uppercase}.value{font-size:19px;font-weight:700}"
               ".file{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:12px 0;border-top:1px solid #334155}"
               ".filename{font-weight:650;overflow-wrap:anywhere}.meta{font-size:13px;color:#94a3b8;margin-top:4px}"
               "@media(max-width:720px){.sidebar{position:static;width:auto;display:flex;flex-wrap:wrap;padding:10px}.brand{width:100%;margin:4px 10px}.navlink{padding:8px 10px}.content{margin:0;padding:14px}}</style><title>");
  s += title;
  s += F("</title></head><body><nav class='sidebar'><div class='brand'>");
  s += htmlEscape(configuredDeviceName);
  s += F("</div><a class='navlink' href='/'>Controls</a><a class='navlink' href='/upload-page'>Upload</a><a class='navlink' href='/files'>Channels &amp; files</a><a class='navlink' href='/sd-health'>SD health</a><a class='navlink' href='/settings'>Settings</a><a class='navlink' href='/backup'>Backup &amp; restore</a><a class='navlink' href='/firmware'>Firmware update</a><a class='navlink' href='/diagnostics'>Diagnostics</a></nav><main class='content'>");
  return s;
}

static void sendHomePage() {
  bool uploadPage = webServer.uri() == "/upload-page";
  String page = pageHeader(uploadPage ? "MiniTV Upload" : "MiniTV Control");
  String currentChannel = (video_count == 0 && !has_random) ? "None" : channelLabel(video_idx);
  String channelOptions;
  if (has_random) channelOptions += String("<option value='0'") + (video_idx == 0 ? " selected" : "") + ">Random</option>";
  for (int channel = 1; channel <= video_count; channel++) {
    channelOptions += "<option value='" + String(channel) + "'" + (video_idx == channel ? " selected" : "") + ">" + htmlEscape(channelLabel(channel)) + "</option>";
  }
  page += uploadPage ? F("<h1>Upload media <span style='font-size:14px;color:#94a3b8'>") : F("<h1>MiniTV Controls <span style='font-size:14px;color:#94a3b8'>");
  page += MINITV_VERSION;
  page += F("</span></h1>");
  if (!uploadPage) {
    page += F("<div class='card status'>");
    page += "<div class='stat'><div class='label'>Playing</div><div class='value' id='title' style='font-size:14px;overflow-wrap:anywhere'>" + htmlEscape(current_video_title) + "</div></div>";
    page += "<div class='stat'><div class='label'>Channel</div><div class='value' id='channel'>" + currentChannel + "</div></div>";
    page += "<div class='stat'><div class='label'>Volume</div><div class='value'><span id='volume'>" + String(audio_volume) + "</span>%</div></div>";
    page += "<div class='stat'><div class='label'>Audio</div><div class='value' id='mute'>" + String(is_muted ? "Muted" : "On") + "</div></div>";
    page += "<div class='stat'><div class='label'>Brightness</div><div class='value'><span id='brightness'>" + String(screen_brightness) + "</span>/255</div></div>";
    page += "<div class='stat' id='sleepStat' style='display:none'><div class='label'>Sleep timer</div><div class='value' id='sleepCountdown'>Off</div></div></div>";
    page += F("<div class='card'><h2>Playback</h2><div class='row'>"
            "<button onclick=\"action('prev')\">&larr; Previous</button><button onclick=\"action('next')\">Next &rarr;</button>"
            "<button onclick=\"action('play')\">Play / Resume</button><button onclick=\"action('mute')\">Mute / Unmute</button><button class='danger' onclick=\"action('stop')\">Stop</button></div>"
            "<div class='row'>Channel <select id='playChannel'>");
  page += channelOptions;
  page += F("</select><button onclick=\"action('channel','&channel='+document.getElementById('playChannel').value)\">Play channel</button></div>"
            "<div>Volume <input id='volumeSlider' type='range' min='0' max='200' step='5' value='");
  page += String(audio_volume);
  page += F("' oninput='v.textContent=this.value' onchange='setVolume(this.value)'><span id='v'>");
  page += String(audio_volume);
  page += F("</span>%</div><div>Brightness <input id='brightnessSlider' type='range' min='0' max='255' step='5' value='");
  page += String(screen_brightness);
  page += F("' oninput='b.textContent=this.value' onchange='setBrightness(this.value)'><span id='b'>");
  page += String(screen_brightness);
    page += F("</span>/255</div><div class='row'><button class='danger' onclick='restartMiniTV()'>Restart MiniTV</button></div><div id='message'></div></div>");
  }

  if (uploadPage) {
    page += F("<div class='card' id='upload'><h2>Upload to SD card</h2>"
            "<p>Playback stops during an upload. Matching video and audio files must have the same base filename.</p>"
            "<form id='up' method='POST' enctype='multipart/form-data'><div class='row'>"
            "Channel <select id='uploadChannel' onchange=\"document.getElementById('newChannel').style.display=this.value==='new'?'inline-block':'none'\">");
  page += channelOptions;
    page += F("<option value='new'>New channel...</option></select>"
            "<input id='newChannel' type='number' min='1' placeholder='Number' style='width:100px;display:none' value='");
    page += String(video_count + 1);
    page += F("'><input type='file' name='files' accept='.mjpeg,.mp3' multiple required><button>Upload selected files</button></div>"
            "<label style='display:block;margin:12px 4px'><input id='autoChannels' type='checkbox'> Put each matching video/audio pair in consecutive channels, starting with the selected channel</label></form>"
            "<progress id='progress' value='0' max='100' style='display:none'></progress><button id='cancelUpload' class='danger' style='display:none' type='button' onclick='cancelUpload()'>Cancel upload</button><div id='uploadStatus'></div>"
            "<p>Select the matching .mjpeg and .mp3 files together. On Windows, hold Ctrl while selecting both files.</p></div>");
  }
  page += F("<script>const E=id=>document.getElementById(id),BLOCK=32768;let uploadCancelled=false,activeRequest=null,uploadStart=0;function msg(t){E('message').textContent=t}"
             "function action(a,x=''){msg('Sending command...');fetch('/action?do='+a+x).then(()=>refreshStatus()).catch(()=>msg('MiniTV is changing...'))}"
             "function setVolume(n){fetch('/volume?value='+n).then(()=>refreshStatus())}"
             "function setBrightness(n){fetch('/brightness?value='+n).then(()=>refreshStatus())}"
             "function restartMiniTV(){if(confirm('Restart the MiniTV?'))fetch('/restart',{method:'POST'}).then(()=>{msg('MiniTV is restarting...');reloadWhenReady()})}"
             "function put(id,v){let e=E(id);if(e)e.textContent=v}function val(id,v){let e=E(id);if(e)e.value=v}"
             "function refreshStatus(){fetch('/status',{cache:'no-store'}).then(r=>r.json()).then(s=>{put('channel',s.channel);put('volume',s.volume);put('mute',s.muted?'Muted':'On');val('volumeSlider',s.volume);put('v',s.volume);put('brightness',s.brightness);val('brightnessSlider',s.brightness);put('b',s.brightness);put('title',s.title);put('rssi',s.rssi);put('heap',s.heapKB);put('uptime',s.uptime);let st=E('sleepStat');if(st){st.style.display=(s.sleepSeconds||s.sleeping)?'block':'none';let n=s.sleepSeconds||0,h=Math.floor(n/3600),m=Math.floor((n%3600)/60),q=n%60;put('sleepCountdown',s.sleeping?'Sleeping':(h?h+'h ':'')+m+'m '+q+'s')}}).catch(()=>{})}"
             "function reloadWhenReady(){setTimeout(()=>fetch('/status',{cache:'no-store'}).then(r=>{if(r.ok)location.reload();else reloadWhenReady()}).catch(reloadWhenReady),1200)}"
             "function cancelUpload(){if(!confirm('Cancel this upload?'))return;uploadCancelled=true;if(activeRequest)activeRequest.abort();E('uploadStatus').textContent='Cancelling upload...';fetch('/cancel-upload',{method:'POST'}).finally(reloadWhenReady)}"
             "function sendBlock(f,o,c,done,total){return new Promise((ok,bad)=>{let b=f.slice(o,Math.min(o+BLOCK,f.size)),d=new FormData(),x=new XMLHttpRequest(),settled=false;"
            "activeRequest=x;d.append('files',b,f.name);x.open('POST','/upload?channel='+encodeURIComponent(c)+'&chunked=1&offset='+o+'&final=0');"
            "x.upload.onprogress=e=>{if(e.lengthComputable){let bytes=done+e.loaded,n=Math.round(bytes*100/total),secs=(performance.now()-uploadStart)/1000,rate=secs>0?bytes/secs:0,eta=rate>0?Math.ceil((total-bytes)/rate):0;E('progress').value=n;E('uploadStatus').textContent='Uploading '+f.name+'... '+n+'% · '+(rate/1048576).toFixed(2)+' MB/s · '+eta+' sec remaining'}};"
             "let good=n=>{n=Number(n);if(!settled&&n>o){settled=true;ok(Math.min(n-o,b.size))}},fail=()=>{if(!settled){settled=true;bad(Error('Connection interrupted'))}},check=()=>fetch('/upload-size?channel='+encodeURIComponent(c)+'&file='+encodeURIComponent(f.name),{cache:'no-store'}).then(r=>r.text()).then(n=>Number(n)>o?good(n):fail()).catch(fail);"
             "x.timeout=60000;x.onload=()=>x.status<400?good(x.responseText):check();x.ontimeout=fail;x.onerror=check;x.send(d)})}"
             "async function sendReliable(f,o,c,done,total){for(let attempt=1;attempt<=5;attempt++){if(uploadCancelled)throw Error('Cancelled');try{return await sendBlock(f,o,c,done,total)}catch(err){if(uploadCancelled)throw err;E('uploadStatus').textContent='SD write retry '+attempt+' of 5...';await new Promise(r=>setTimeout(r,500))}}throw Error('SD write failed')}"
            "setInterval(refreshStatus,2000);let uploadForm=E('up');if(uploadForm)uploadForm.onsubmit=async function(e){e.preventDefault();let p=E('progress'),u=E('uploadStatus'),c=E('uploadChannel').value;"
            "if(c==='new')c=E('newChannel').value;if(!c){u.textContent='Choose a channel number';return;}let fs=Array.from(E('up').querySelector('input[type=file]').files),auto=E('autoChannels').checked;"
            "let base=f=>f.name.replace(/\\.(mjpeg|mp3)$/i,'').toLowerCase(),pairs=[];fs.forEach(f=>{let k=base(f);if(!pairs.includes(k))pairs.push(k)});if(auto&&(!/^\\d+$/.test(c)||Number(c)<1)){u.textContent='Choose a numeric starting channel';return}let fileChannel=f=>auto?String(Number(c)+pairs.indexOf(base(f))):c;"
            "if(!fs.length)return;let total=fs.reduce((n,f)=>n+f.size,0),done=0;uploadCancelled=false;uploadStart=performance.now();p.style.display='block';p.value=0;E('cancelUpload').style.display='inline-block';"
            "try{let storage=await fetch('/storage',{cache:'no-store'}).then(r=>r.json());if(Math.ceil(total/1024)+1024>storage.freeKB){p.style.display='none';E('cancelUpload').style.display='none';u.textContent='Not enough free space on the SD card.';return}let exists=false;for(let f of fs){let fc=fileChannel(f),r=await fetch('/file-exists?channel='+encodeURIComponent(fc)+'&file='+encodeURIComponent(f.name),{cache:'no-store'});if((await r.text())==='1')exists=true}if(exists&&!confirm('Existing files with these names will be replaced. Continue?')){p.style.display='none';E('cancelUpload').style.display='none';u.textContent='Upload cancelled.';return}u.textContent='Stopping playback and preparing SD card...';let ready=await fetch('/prepare-upload',{method:'POST'});if(!ready.ok)throw Error('Audio did not stop');for(let i=0;i<fs.length;i++){let f=fs[i],fc=fileChannel(f),o=0;while(o<f.size){let wrote=await sendReliable(f,o,fc,done,total);o+=wrote;done+=wrote}}"
            "let finish=await fetch('/upload-finish',{method:'POST'});if(!finish.ok)throw Error('Finish failed');p.value=100;E('cancelUpload').style.display='none';u.textContent='Upload complete. MiniTV is restarting...';reloadWhenReady()}catch(err){if(!uploadCancelled){E('cancelUpload').style.display='none';u.textContent='Upload interrupted. MiniTV is restarting so you can try again.';fetch('/restart',{method:'POST'}).finally(reloadWhenReady)}}};refreshStatus();</script></body></html>");
  webServer.send(200, "text/html", page);
}

static void sendStatus() {
  String json = "{\"channel\":\"";
  json += (video_count == 0 && !has_random) ? "None" : jsonEscape(channelLabel(video_idx));
  json += "\",\"volume\":" + String(audio_volume);
  json += ",\"brightness\":" + String(screen_brightness);
  json += ",\"muted\":" + String(is_muted ? "true" : "false");
  json += ",\"playing\":" + String(playback_active ? "true" : "false");
  json += ",\"sleeping\":" + String(screen_sleeping ? "true" : "false");
  uint32_t sleepRemaining = 0;
  uint32_t sleepSeconds = 0;
  if (sleep_deadline_ms && (int32_t)(sleep_deadline_ms - millis()) > 0) {
    sleepSeconds = (sleep_deadline_ms - millis() + 999UL) / 1000UL;
    sleepRemaining = (sleepSeconds + 59UL) / 60UL;
  }
  json += ",\"sleepMinutes\":" + String(sleepRemaining);
  json += ",\"sleepSeconds\":" + String(sleepSeconds);
  json += ",\"title\":\"" + jsonEscape(current_video_title) + "\"";
  json += ",\"deviceName\":\"" + jsonEscape(configuredDeviceName) + "\"";
  json += ",\"hostname\":\"" + jsonEscape(configuredHostname) + ".local\"";
  json += ",\"rssi\":" + String(WiFi.RSSI());
  json += ",\"heapKB\":" + String(ESP.getFreeHeap() / 1024);
  json += ",\"uptime\":" + String(millis() / 1000) + "}";
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", json);
}

static void wakeMiniTV() {
  sleep_deadline_ms = 0;
  screen_sleeping = false;
  ledcWrite(0, screen_brightness);
#ifdef AUDIO_ENABLE_PIN
  digitalWrite(AUDIO_ENABLE_PIN, LOW);
#endif
  playback_paused = false;
  logEvent("Display and playback awakened");
}

static void sleepMiniTV() {
  sleep_deadline_ms = 0;
  screen_sleeping = true;
  playback_paused = true;
  web_stop_requested = true;
  audio_stop_requested = true;
  ledcWrite(0, 0);
#ifdef AUDIO_ENABLE_PIN
  digitalWrite(AUDIO_ENABLE_PIN, HIGH);
#endif
  Serial.println("Sleep timer expired: playback, display, and amplifier stopped");
  logEvent("Sleep mode entered");
}

static void sendSettingsPage() {
  uint32_t remaining = 0;
  if (sleep_deadline_ms && (int32_t)(sleep_deadline_ms - millis()) > 0) {
    remaining = (sleep_deadline_ms - millis() + 59999UL) / 60000UL;
  }
  String page = pageHeader("MiniTV Settings");
  page += "<h1>Settings <span style='font-size:14px;color:#94a3b8'>" + String(MINITV_VERSION) + "</span></h1>";
  page += "<div class='card'><h2>Sleep timer</h2><p>Stop playback, turn off the screen, and disable the speaker after a set time.</p>";
  if (screen_sleeping) page += "<p><strong>The MiniTV is sleeping.</strong></p>";
  else if (remaining) page += "<p><strong>Sleep in about " + String(remaining) + " minute(s).</strong></p>";
  else page += "<p class='meta'>No sleep timer is active.</p>";
  page += F("<div class='row'>"
            "<form method='POST' action='/sleep'><input type='hidden' name='minutes' value='15'><button>15 minutes</button></form>"
            "<form method='POST' action='/sleep'><input type='hidden' name='minutes' value='30'><button>30 minutes</button></form>"
            "<form method='POST' action='/sleep'><input type='hidden' name='minutes' value='60'><button>1 hour</button></form>"
            "<form method='POST' action='/sleep'><input type='hidden' name='minutes' value='120'><button>2 hours</button></form></div>"
            "<form class='row' method='POST' action='/sleep'>Custom <input type='number' name='minutes' min='1' max='1440' value='45' style='width:100px'> minutes <button>Set timer</button></form>"
            "<div class='row'><form method='POST' action='/wake'><button type='submit'>Wake now / cancel timer</button></form></div></div>");
  page += F("<div class='card'><h2>Display and audio</h2><p>Brightness and volume can be adjusted live from the Controls page. Your settings are saved automatically.</p><a href='/'>Open controls</a></div>");
  page += "<div class='card'><h2>Device identity</h2><p>Give this TV a recognizable name and its own local-network address.</p>";
  page += "<form method='POST' action='/identity-settings'><label>Display name</label><br><input type='text' name='device_name' maxlength='32' required value='" + htmlEscape(configuredDeviceName) + "' style='min-width:260px'><br>";
  page += "<label>Network hostname</label><br><input type='text' name='hostname' maxlength='32' pattern='[A-Za-z0-9][A-Za-z0-9-]*[A-Za-z0-9]' required value='" + htmlEscape(configuredHostname) + "' style='min-width:260px'>.local";
  page += "<p class='meta'>Use letters, numbers, and hyphens only. Saving restarts the MiniTV.</p><button type='submit'>Save device identity</button></form></div>";
  page += "<div class='card'><h2>Administrator protection</h2><p>Status: <strong>" + String(configuredAdminPassword.length() ? "Enabled" : "Disabled") + "</strong></p>";
  page += F("<p class='meta'>When enabled, the username is <strong>admin</strong>. Your browser will request the password before opening any MiniTV page. The password is stored only on this device and is excluded from settings backups. If it is forgotten, hold the board button for three seconds while powering on to clear it.</p>"
            "<form method='POST' action='/admin-settings'><label>New password</label><br><input type='password' name='password' minlength='8' maxlength='63' placeholder='At least 8 characters' style='min-width:260px'><br>"
            "<label>Confirm password</label><br><input type='password' name='confirm' minlength='8' maxlength='63' style='min-width:260px'><br>"
            "<label><input type='checkbox' name='disable' value='1'> Disable administrator protection</label><br>"
            "<button type='submit' onclick=\"return confirm('Save administrator security settings and restart MiniTV?')\">Save security settings</button></form></div>");
  page += "<div class='card'><h2>Wi-Fi network</h2><p class='meta'>Changing the network restarts the MiniTV. Leave the password blank to keep the saved password.</p>";
  page += "<form method='POST' action='/wifi-settings'><label>Network name (SSID)</label><br><input type='text' name='ssid' maxlength='32' required value='" + htmlEscape(configuredSSID) + "' style='min-width:260px'><br>";
  page += "<label>Password</label><br><input type='password' name='password' maxlength='64' placeholder='Leave blank to keep current password' style='min-width:260px'><br><button type='submit' onclick=\"return confirm('Save Wi-Fi settings and restart MiniTV?')\">Save Wi-Fi and restart</button></form></div>";
  time_t now = time(nullptr);
  struct tm localNow;
  char timeText[40] = "Waiting for network time";
  if (now > 1700000000 && localtime_r(&now, &localNow)) strftime(timeText, sizeof(timeText), "%a %b %d, %I:%M:%S %p", &localNow);
  page += "<div class='card'><h2>Daily schedule</h2><p>Device time: <strong>" + String(timeText) + "</strong></p>";
  page += "<form method='POST' action='/schedule-settings'><label><input type='checkbox' name='enabled' value='1'" + String(dailyScheduleEnabled ? " checked" : "") + "> Enable daily sleep and wake schedule</label><br>";
  page += "<label>Sleep at <input type='time' name='sleep_time' value='" + htmlEscape(dailySleepTime) + "'></label> ";
  page += "<label>Wake at <input type='time' name='wake_time' value='" + htmlEscape(dailyWakeTime) + "'></label><br>";
  page += "<label>Timezone rule <input type='text' name='timezone' maxlength='63' value='" + htmlEscape(configuredTimezone) + "' style='min-width:300px'></label>";
  page += F("<p class='meta'>Default is US Central time with automatic daylight-saving changes. A different region needs its POSIX timezone rule.</p><button type='submit'>Save schedule</button></form></div></main></body></html>");
  webServer.send(200, "text/html", page);
}

static void handleSleepTimer() {
  uint32_t minutes = constrain(webServer.arg("minutes").toInt(), 1, 1440);
  wakeMiniTV();
  sleep_deadline_ms = millis() + minutes * 60000UL;
  webServer.sendHeader("Location", "/settings");
  webServer.send(303);
}

static void handleWake() {
  wakeMiniTV();
  webServer.sendHeader("Location", "/settings");
  webServer.send(303);
}

static bool validClockTime(const String &value) {
  if (value.length() != 5 || value.charAt(2) != ':') return false;
  if (!isDigit(value.charAt(0)) || !isDigit(value.charAt(1)) ||
      !isDigit(value.charAt(3)) || !isDigit(value.charAt(4))) return false;
  int hour = value.substring(0, 2).toInt();
  int minute = value.substring(3).toInt();
  return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

static void handleWiFiSettings() {
  String ssid = webServer.arg("ssid");
  ssid.trim();
  if (!ssid.length()) {
    webServer.send(400, "text/plain", "Network name is required");
    return;
  }
  String password = webServer.arg("password");
  preferences.begin(APP_NAME, false);
  preferences.putString("wifi_ssid", ssid);
  if (password.length()) preferences.putString("wifi_pass", password);
  preferences.end();
  String page = pageHeader("Wi-Fi saved");
  page += "<h1>Wi-Fi settings saved</h1><div class='card'><p>The MiniTV is restarting and will connect to <strong>" + htmlEscape(ssid) + "</strong>.</p><p>If that network cannot be reached, it will fall back to the Wi-Fi details compiled into config.h.</p></div></main></body></html>";
  webServer.send(200, "text/html", page);
  requestRestart();
}

static void handleAdminSettings() {
  bool disable = webServer.hasArg("disable");
  String password = webServer.arg("password");
  String confirmation = webServer.arg("confirm");
  if (!disable && (password.length() < 8 || password != confirmation)) {
    webServer.send(400, "text/plain", "Passwords must match and contain at least 8 characters");
    return;
  }
  preferences.begin(APP_NAME, false);
  if (disable) preferences.remove("admin_pass");
  else preferences.putString("admin_pass", password);
  preferences.end();
  String page = pageHeader("Security settings saved");
  page += disable ? "<h1>Administrator protection disabled</h1>" : "<h1>Administrator protection enabled</h1>";
  page += "<div class='card'><p>The MiniTV is restarting. ";
  page += disable ? "The web interface will no longer request a password." : "The username is <strong>admin</strong>. Your browser will request the new password when you reconnect.";
  page += "</p></div></main></body></html>";
  webServer.send(200, "text/html", page);
  requestRestart();
}

static String cleanHostname(String value) {
  value.trim();
  value.toLowerCase();
  String clean;
  clean.reserve(value.length());
  bool lastWasDash = false;
  for (size_t i = 0; i < value.length() && clean.length() < 32; i++) {
    char c = value.charAt(i);
    bool validCharacter = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (validCharacter) {
      clean += c;
      lastWasDash = false;
    } else if ((c == '-' || c == ' ' || c == '_') && clean.length() && !lastWasDash) {
      clean += '-';
      lastWasDash = true;
    }
  }
  while (clean.endsWith("-")) clean.remove(clean.length() - 1);
  return clean;
}

static void handleIdentitySettings() {
  String deviceName = webServer.arg("device_name");
  deviceName.trim();
  String hostname = cleanHostname(webServer.arg("hostname"));
  if (!deviceName.length() || hostname.length() < 2) {
    webServer.send(400, "text/plain", "Enter a display name and a valid hostname");
    return;
  }
  preferences.begin(APP_NAME, false);
  preferences.putString("device_name", deviceName);
  preferences.putString("hostname", hostname);
  preferences.end();
  String page = pageHeader("Device identity saved");
  page += "<h1>Device identity saved</h1><div class='card'><p>This TV will restart as <strong>" + htmlEscape(deviceName) + "</strong>.</p>";
  page += "<p>Its new address will be <strong>http://" + htmlEscape(hostname) + ".local</strong>. Its numeric IP address may remain the same.</p></div></main></body></html>";
  webServer.send(200, "text/html", page);
  requestRestart();
}

static void handleScheduleSettings() {
  String sleepAt = webServer.arg("sleep_time");
  String wakeAt = webServer.arg("wake_time");
  String timezone = webServer.arg("timezone");
  timezone.trim();
  if (!validClockTime(sleepAt) || !validClockTime(wakeAt) || !timezone.length()) {
    webServer.send(400, "text/plain", "Enter valid sleep, wake, and timezone values");
    return;
  }
  dailyScheduleEnabled = webServer.hasArg("enabled");
  dailySleepTime = sleepAt;
  dailyWakeTime = wakeAt;
  configuredTimezone = timezone;
  preferences.begin(APP_NAME, false);
  preferences.putBool("sched_en", dailyScheduleEnabled);
  preferences.putString("sleep_h", dailySleepTime);
  preferences.putString("wake_h", dailyWakeTime);
  preferences.putString("timezone", configuredTimezone);
  preferences.end();
  configTzTime(configuredTimezone.c_str(), "pool.ntp.org", "time.nist.gov");
  webServer.sendHeader("Location", "/settings");
  webServer.send(303);
}

static String jsonStringValue(const String &document, const String &key, const String &fallback = "") {
  String token = "\"" + key + "\"";
  int position = document.indexOf(token);
  if (position < 0) return fallback;
  position = document.indexOf(':', position + token.length());
  if (position < 0) return fallback;
  position++;
  while (position < (int)document.length() && isspace((unsigned char)document.charAt(position))) position++;
  if (position >= (int)document.length() || document.charAt(position) != '"') return fallback;
  position++;
  String value;
  bool escaped = false;
  for (; position < (int)document.length(); position++) {
    char c = document.charAt(position);
    if (escaped) {
      if (c == 'n') value += '\n';
      else if (c == 'r') value += '\r';
      else if (c == 't') value += '\t';
      else value += c;
      escaped = false;
    } else if (c == '\\') escaped = true;
    else if (c == '"') return value;
    else value += c;
  }
  return fallback;
}

static int jsonIntValue(const String &document, const String &key, int fallback) {
  String token = "\"" + key + "\"";
  int position = document.indexOf(token);
  if (position < 0) return fallback;
  position = document.indexOf(':', position + token.length());
  if (position < 0) return fallback;
  position++;
  while (position < (int)document.length() && isspace((unsigned char)document.charAt(position))) position++;
  int end = position;
  if (end < (int)document.length() && document.charAt(end) == '-') end++;
  while (end < (int)document.length() && isDigit(document.charAt(end))) end++;
  if (end == position) return fallback;
  return document.substring(position, end).toInt();
}

static bool jsonBoolValue(const String &document, const String &key, bool fallback) {
  String token = "\"" + key + "\"";
  int position = document.indexOf(token);
  if (position < 0) return fallback;
  position = document.indexOf(':', position + token.length());
  if (position < 0) return fallback;
  position++;
  while (position < (int)document.length() && isspace((unsigned char)document.charAt(position))) position++;
  if (document.substring(position, position + 4) == "true") return true;
  if (document.substring(position, position + 5) == "false") return false;
  return fallback;
}

static void sendSettingsBackup() {
  String backup = "{\n  \"format\":\"MiniTV-settings\",\n  \"formatVersion\":1,";
  backup += "\n  \"firmware\":\"" + jsonEscape(MINITV_VERSION) + "\",";
  backup += "\n  \"deviceName\":\"" + jsonEscape(configuredDeviceName) + "\",";
  backup += "\n  \"hostname\":\"" + jsonEscape(configuredHostname) + "\",";
  backup += "\n  \"volume\":" + String(audio_volume) + ",";
  backup += "\n  \"brightness\":" + String(screen_brightness) + ",";
  backup += "\n  \"muted\":" + String(is_muted ? "true" : "false") + ",";
  backup += "\n  \"currentChannel\":" + String(video_idx) + ",";
  backup += "\n  \"scheduleEnabled\":" + String(dailyScheduleEnabled ? "true" : "false") + ",";
  backup += "\n  \"sleepTime\":\"" + jsonEscape(dailySleepTime) + "\",";
  backup += "\n  \"wakeTime\":\"" + jsonEscape(dailyWakeTime) + "\",";
  backup += "\n  \"timezone\":\"" + jsonEscape(configuredTimezone) + "\",";
  backup += "\n  \"channelLabels\":{";
  Preferences labels;
  labels.begin(APP_NAME, true);
  bool firstLabel = true;
  for (int channel = 1; channel <= video_count; channel++) {
    String label = labels.getString(("ch_" + String(channel)).c_str(), "");
    if (!label.length()) continue;
    if (!firstLabel) backup += ',';
    backup += "\n    \"" + String(channel) + "\":\"" + jsonEscape(label) + "\"";
    firstLabel = false;
  }
  labels.end();
  if (!firstLabel) backup += '\n';
  backup += "  }\n}\n";
  String filename = configuredHostname + "-settings.json";
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", backup);
}

static bool applySettingsRestore(const String &document, String &error) {
  if (jsonStringValue(document, "format") != "MiniTV-settings" ||
      jsonIntValue(document, "formatVersion", 0) != 1) {
    error = "This is not a supported MiniTV settings backup";
    return false;
  }
  String deviceName = jsonStringValue(document, "deviceName", configuredDeviceName);
  deviceName.trim();
  if (deviceName.length() > 32) deviceName = deviceName.substring(0, 32);
  String hostname = cleanHostname(jsonStringValue(document, "hostname", configuredHostname));
  String sleepAt = jsonStringValue(document, "sleepTime", dailySleepTime);
  String wakeAt = jsonStringValue(document, "wakeTime", dailyWakeTime);
  String timezone = jsonStringValue(document, "timezone", configuredTimezone);
  if (!deviceName.length() || hostname.length() < 2 || !validClockTime(sleepAt) ||
      !validClockTime(wakeAt) || !timezone.length() || timezone.length() > 63) {
    error = "The backup contains invalid identity or schedule settings";
    return false;
  }
  int restoredVolume = constrain(jsonIntValue(document, "volume", audio_volume), 0, 200);
  int restoredBrightness = constrain(jsonIntValue(document, "brightness", screen_brightness), 0, 255);
  int restoredChannel = constrain(jsonIntValue(document, "currentChannel", video_idx), has_random ? 0 : 1, max(1, video_count));
  preferences.begin(APP_NAME, false);
  preferences.putString("device_name", deviceName);
  preferences.putString("hostname", hostname);
  preferences.putInt("volume", restoredVolume);
  preferences.putInt("brightness", restoredBrightness);
  preferences.putBool(K_MUTE, jsonBoolValue(document, "muted", is_muted));
  preferences.putInt(K_VIDEO_INDEX, restoredChannel);
  preferences.putBool("sched_en", jsonBoolValue(document, "scheduleEnabled", dailyScheduleEnabled));
  preferences.putString("sleep_h", sleepAt);
  preferences.putString("wake_h", wakeAt);
  preferences.putString("timezone", timezone);
  preferences.end();
  int labelsPosition = document.indexOf("\"channelLabels\"");
  String labelsDocument = labelsPosition >= 0 ? document.substring(labelsPosition) : "";
  Preferences labels;
  labels.begin(APP_NAME, false);
  for (int channel = 1; channel <= video_count; channel++) {
    String key = "ch_" + String(channel);
    String label = jsonStringValue(labelsDocument, String(channel), "");
    if (label.length() > 32) label = label.substring(0, 32);
    if (label.length()) labels.putString(key.c_str(), label);
    else labels.remove(key.c_str());
  }
  labels.end();
  return true;
}

static void sendBackupPage() {
  String page = pageHeader("MiniTV Backup & Restore");
  page += "<h1>Backup &amp; restore <span style='font-size:14px;color:#94a3b8'>" + String(MINITV_VERSION) + "</span></h1>";
  page += F("<div class='card'><h2>Download settings</h2><p>Save device identity, channel labels, playback settings, and the daily schedule as a small JSON file.</p>"
            "<p class='meta'>Wi-Fi credentials, videos, audio files, and temporary sleep timers are not included.</p><a href='/settings-backup'><button>Download settings backup</button></a></div>"
            "<div class='card'><h2>Restore settings</h2><p>Select a MiniTV settings JSON file. The file is validated before anything is saved.</p>"
            "<form id='restoreForm'><input id='restoreFile' type='file' name='settings' accept='.json,application/json' required><button type='submit'>Restore settings</button></form><div id='restoreStatus'></div></div>"
            "<script>const f=document.getElementById('restoreForm'),s=document.getElementById('restoreStatus');f.onsubmit=e=>{e.preventDefault();let file=document.getElementById('restoreFile').files[0];if(!file)return;if(!confirm('Restore settings from '+file.name+' and restart MiniTV?'))return;let d=new FormData();d.append('settings',file,file.name);s.textContent='Validating backup...';fetch('/settings-restore',{method:'POST',body:d}).then(async r=>{let t=await r.text();if(!r.ok)throw Error(t);s.textContent='Settings restored. MiniTV is restarting...';setTimeout(wait,1800)}).catch(e=>s.textContent='Restore failed: '+e.message)};function wait(){fetch('/status',{cache:'no-store'}).then(r=>{if(r.ok)location.href='/settings';else setTimeout(wait,1200)}).catch(()=>setTimeout(wait,1200))}</script></main></body></html>");
  webServer.send(200, "text/html", page);
}

static void handleRestoreData() {
  HTTPUpload &upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    restoreUploadJson = "";
    restoreUploadJson.reserve(8192);
    restoreUploadOK = false;
    restoreUploadError = "";
    String filename = upload.filename;
    filename.toLowerCase();
    if (!filename.endsWith(".json")) restoreUploadError = "Select a .json settings backup";
  } else if (upload.status == UPLOAD_FILE_WRITE && !restoreUploadError.length()) {
    if (restoreUploadJson.length() + upload.currentSize > 16384) restoreUploadError = "Settings backup is too large";
    else restoreUploadJson.concat((const char *)upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END && !restoreUploadError.length()) {
    restoreUploadOK = applySettingsRestore(restoreUploadJson, restoreUploadError);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    restoreUploadError = "Restore upload was interrupted";
  }
}

static void handleRestoreComplete() {
  if (!restoreUploadOK) {
    webServer.send(400, "text/plain", restoreUploadError.length() ? restoreUploadError : "Restore failed");
    return;
  }
  webServer.send(200, "text/plain", "Settings restored; restarting");
  requestRestart();
}

static void sendFirmwarePage() {
  String page = pageHeader("MiniTV Firmware Update");
  page += "<h1>Firmware update <span style='font-size:14px;color:#94a3b8'>" + String(MINITV_VERSION) + "</span></h1>";
  page += F("<div class='card'><h2>Install a compiled firmware file</h2>"
            "<p>Select the main <strong>MiniTV.ino.bin</strong> application file produced by Arduino IDE. Do not select a bootloader or partitions file. Playback stops while it is installed, and the MiniTV restarts only after the image passes validation.</p>"
            "<p class='meta'>Keep the MiniTV powered on and stay on this page until the update finishes. This updates the application only; videos and settings are retained.</p>"
            "<form id='firmwareForm'><input id='firmwareFile' type='file' name='firmware' accept='.bin,application/octet-stream' required><button type='submit'>Install firmware</button></form>"
            "<progress id='firmwareProgress' value='0' max='100' style='display:none'></progress><div id='firmwareStatus'></div></div>"
            "<div class='card'><h2>Important</h2><p>If the board was compiled with a partition layout that has no OTA application slot, installation will be rejected safely. Use the same board and partition settings as the currently working build.</p></div>"
            "<script>const f=document.getElementById('firmwareForm'),p=document.getElementById('firmwareProgress'),s=document.getElementById('firmwareStatus');"
            "f.onsubmit=e=>{e.preventDefault();let file=document.getElementById('firmwareFile').files[0];if(!file)return;let name=file.name.toLowerCase();if(!name.endsWith('.bin')||name.includes('bootloader')||name.includes('partitions')){s.textContent='Select the main MiniTV.ino.bin application file, not a bootloader or partitions file.';return}"
            "if(!confirm('Install '+file.name+'? Do not remove power during the update.'))return;let d=new FormData();d.append('firmware',file,file.name);let x=new XMLHttpRequest();p.style.display='block';p.value=0;s.textContent='Stopping playback and uploading firmware...';"
            "x.open('POST','/firmware-update');x.upload.onprogress=e=>{if(e.lengthComputable){let n=Math.round(e.loaded*100/e.total);p.value=n;s.textContent='Uploading firmware... '+n+'%'}};"
            "x.onload=()=>{if(x.status<400){p.value=100;s.textContent='Firmware installed. MiniTV is restarting...';setTimeout(waitForMiniTV,1800)}else{s.textContent='Update rejected: '+x.responseText}};x.onerror=()=>s.textContent='Connection interrupted before the update completed.';x.send(d)};"
            "function waitForMiniTV(){fetch('/status',{cache:'no-store'}).then(r=>{if(r.ok)location.href='/diagnostics';else setTimeout(waitForMiniTV,1200)}).catch(()=>setTimeout(waitForMiniTV,1200))}</script></main></body></html>");
  webServer.send(200, "text/html", page);
}

static void handleFirmwareData() {
  HTTPUpload &upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    firmwareUpdateOK = false;
    firmwareUpdateError = "";
    String filename = safeFilename(upload.filename);
    String lowerFilename = filename;
    lowerFilename.toLowerCase();
    if (!lowerFilename.endsWith(".bin") || lowerFilename.indexOf("bootloader") >= 0 ||
        lowerFilename.indexOf("partitions") >= 0) {
      firmwareUpdateError = "Select the main MiniTV.ino.bin application image";
      return;
    }
    Serial.printf("Firmware update starting: %s\n", filename.c_str());
    logEvent("Firmware update started: %s", filename.c_str());
    playback_paused = true;
    web_stop_requested = true;
    audio_stop_requested = true;
#ifdef AUDIO_ENABLE_PIN
    digitalWrite(AUDIO_ENABLE_PIN, HIGH);
#endif
    uint32_t started = millis();
    while ((audio_task_running || playback_active) && millis() - started < 4500) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (audio_task_handle != NULL) {
      vTaskDelete(audio_task_handle);
      audio_task_handle = NULL;
      audio_task_running = false;
    }
    i2s_stop(I2S_NUM_0);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      firmwareUpdateError = String(Update.errorString());
      Serial.printf("Firmware update begin failed: %s\n", firmwareUpdateError.c_str());
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!firmwareUpdateError.length()) {
      size_t written = Update.write(upload.buf, upload.currentSize);
      if (written != upload.currentSize) {
        firmwareUpdateError = String(Update.errorString());
        Serial.printf("Firmware write failed: %s\n", firmwareUpdateError.c_str());
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!firmwareUpdateError.length() && Update.end(true)) {
      firmwareUpdateOK = true;
      Serial.printf("Firmware update verified: %u bytes\n", (unsigned)upload.totalSize);
      logEvent("Firmware verified: %u bytes", (unsigned)upload.totalSize);
    } else {
      if (!firmwareUpdateError.length()) firmwareUpdateError = String(Update.errorString());
      Serial.printf("Firmware update rejected: %s\n", firmwareUpdateError.c_str());
      logEvent("Firmware rejected: %s", firmwareUpdateError.c_str());
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    firmwareUpdateError = "Upload was cancelled or interrupted";
    Serial.println("Firmware update aborted");
  }
}

static void handleFirmwareComplete() {
  webServer.sendHeader("Connection", "close");
  if (!firmwareUpdateOK) {
    Update.abort();
    i2s_start(I2S_NUM_0);
    playback_paused = false;
#ifdef AUDIO_ENABLE_PIN
    digitalWrite(AUDIO_ENABLE_PIN, LOW);
#endif
    String reason = firmwareUpdateError.length() ? firmwareUpdateError : "Firmware validation failed";
    webServer.send(400, "text/plain", reason);
    return;
  }
  webServer.send(200, "text/plain", "Firmware verified; restarting");
  requestRestart();
}

static void appendChannelCard(String &page, const String &channelName) {
  String directory = "/Videos/" + channelName;
  File dir = SD.open(directory.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  page += "<section class='card'><h2>";
  page += (channelName == "random") ? "Random" : htmlEscape(channelLabel(channelName.toInt()));
  page += "</h2>";
  if (channelName != "random") {
    Preferences labels;
    labels.begin(APP_NAME, true);
    String savedLabel = labels.getString(("ch_" + channelName).c_str(), "");
    labels.end();
    page += "<form class='row' method='POST' action='/channel-label'><input type='hidden' name='channel' value='" + channelName + "'>";
    page += "<input type='text' name='label' maxlength='32' placeholder='Channel name' value='" + htmlEscape(savedLabel) + "'><button type='submit'>Save name</button></form>";
  }
  if (channelName != "random" && video_count > 1) {
    int channelNumber = channelName.toInt();
    page += "<div class='row'><span class='meta'>Move channel:</span>";
    if (channelNumber > 1) {
      page += "<form method='POST' action='/swap-channel'><input type='hidden' name='channel' value='" + channelName + "'><input type='hidden' name='target' value='" + String(channelNumber - 1) + "'><button type='submit'>&uarr; Up</button></form>";
    }
    if (channelNumber < video_count) {
      page += "<form method='POST' action='/swap-channel'><input type='hidden' name='channel' value='" + channelName + "'><input type='hidden' name='target' value='" + String(channelNumber + 1) + "'><button type='submit'>&darr; Down</button></form>";
    }
    page += "</div>";
  }
  int count = 0;
  File item = dir.openNextFile();
  while (item) {
    String itemName = safeFilename(String(item.name()));
    if (!item.isDirectory() && itemName.endsWith(".mjpeg")) {
      count++;
      String baseName = itemName.substring(0, itemName.length() - 6);
      String mp3Path = directory + "/" + baseName + ".mp3";
      String audioType = SD.exists(mp3Path.c_str()) ? "MP3 audio" : "No matching MP3";
      float sizeMB = item.size() / 1048576.0f;
      page += "<div class='file'><div><div class='filename'>" + htmlEscape(baseName) + "</div>";
      page += "<div class='meta'>" + String(sizeMB, 1) + " MB &bull; " + audioType + "</div></div>";
      page += "<div><form class='row' method='POST' action='/rename'>";
      page += "<input type='hidden' name='channel' value='" + htmlEscape(channelName) + "'>";
      page += "<input type='hidden' name='base' value='" + htmlEscape(baseName) + "'>";
      page += "<input type='text' name='new_base' value='" + htmlEscape(baseName) + "' required>";
      page += "<button type='submit'>Rename title</button></form>";
      page += "<form method='POST' action='/delete' onsubmit=\"return confirm('Delete this video and its matching audio file?')\">";
      page += "<input type='hidden' name='channel' value='" + htmlEscape(channelName) + "'>";
      page += "<input type='hidden' name='base' value='" + htmlEscape(baseName) + "'>";
      page += "<label class='meta'><input type='checkbox' name='delete_folder' value='1'> Remove channel if empty</label>";
      page += "<button class='danger' type='submit'>Delete</button></form></div></div>";
    }
    item.close();
    item = dir.openNextFile();
  }
  dir.close();
  if (count == 0) page += F("<div class='meta'>This channel is empty.</div>");
  page += F("</section>");
}

static void sendFilesPage() {
  String page = pageHeader("MiniTV SD Files");
  page += F("<h1>SD card</h1><p><a href='/'>&larr; Back to controls</a></p>");
  if (has_random) appendChannelCard(page, "random");
  for (int channel = 1; channel <= video_count; channel++) appendChannelCard(page, String(channel));
  page += F("</body></html>");
  webServer.send(200, "text/html", page);
}

static bool isNumericChannelName(const String &name) {
  if (!name.length()) return false;
  for (size_t i = 0; i < name.length(); i++) if (!isDigit(name.charAt(i))) return false;
  return name.toInt() > 0;
}

static void appendHealthIssue(String &page, const String &title, const String &detail,
                              const String &path = "", const String &action = "") {
  page += "<div class='file'><div><div class='filename'>" + htmlEscape(title) + "</div><div class='meta'>" + htmlEscape(detail) + "</div></div>";
  if (path.length() && action.length()) {
    page += "<form method='POST' action='/sd-cleanup' onsubmit=\"return confirm('Remove this item from the SD card?')\">";
    page += "<input type='hidden' name='path' value=\"" + htmlEscape(path) + "\"><input type='hidden' name='action' value='" + action + "'>";
    page += "<button class='danger' type='submit'>Remove</button></form>";
  }
  page += "</div>";
}

static void sendSDHealthPage() {
  bool resumePlayback = !playback_paused;
  playback_paused = true;
  web_stop_requested = true;
  audio_stop_requested = true;
  uint32_t stopStarted = millis();
  while (playback_active && millis() - stopStarted < 4000) vTaskDelay(pdMS_TO_TICKS(10));
  String page = pageHeader("MiniTV SD Health");
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  int issueCount = 0;
  int videoFiles = 0;
  int audioFiles = 0;
  page += "<h1>SD health</h1><p class='meta'>Playback is briefly paused while the card is scanned safely.</p><div class='card status'>";
  page += "<div class='stat'><div class='label'>Total</div><div class='value'>" + String((unsigned)(total / 1048576ULL)) + " MB</div></div>";
  page += "<div class='stat'><div class='label'>Used</div><div class='value'>" + String((unsigned)(used / 1048576ULL)) + " MB</div></div>";
  page += "<div class='stat'><div class='label'>Free</div><div class='value'>" + String((unsigned)((total > used ? total - used : 0) / 1048576ULL)) + " MB</div></div></div>";
  page += "<div class='card'><h2>Audit results</h2>";
  File videosRoot = SD.open("/Videos");
  if (!videosRoot || !videosRoot.isDirectory()) {
    appendHealthIssue(page, "Videos folder missing", "Create /Videos and add channel folders before playback.");
    issueCount++;
  } else {
    File channelEntry = videosRoot.openNextFile();
    while (channelEntry) {
      String channelName = safeFilename(String(channelEntry.name()));
      String channelPath = "/Videos/" + channelName;
      if (!channelEntry.isDirectory()) {
        appendHealthIssue(page, "Unexpected file in /Videos", channelName, channelPath, "file");
        issueCount++;
      } else {
        bool validChannel = channelName == "random" || isNumericChannelName(channelName);
        if (!validChannel) {
          appendHealthIssue(page, "Unrecognized channel folder", channelName + " is not a numbered channel or random folder.");
          issueCount++;
        }
        File channelDir = SD.open(channelPath.c_str());
        int entryCount = 0;
        int channelVideos = 0;
        if (channelDir && channelDir.isDirectory()) {
          File media = channelDir.openNextFile();
          while (media) {
            String filename = safeFilename(String(media.name()));
            String mediaPath = channelPath + "/" + filename;
            if (media.isDirectory()) {
              appendHealthIssue(page, "Nested folder is not supported", mediaPath);
              issueCount++;
            } else {
              entryCount++;
              String lower = filename;
              lower.toLowerCase();
              if (lower.endsWith(".mjpeg")) {
                videoFiles++;
                channelVideos++;
                String base = filename.substring(0, filename.length() - 6);
                bool paired = SD.exists((channelPath + "/" + base + ".mp3").c_str()) ||
                              SD.exists((channelPath + "/" + base + ".aac").c_str());
                if (!paired) {
                  appendHealthIssue(page, "Video has no matching audio", channelName + "/" + filename);
                  issueCount++;
                }
              } else if (lower.endsWith(".mp3") || lower.endsWith(".aac")) {
                audioFiles++;
                String base = filename.substring(0, filename.length() - 4);
                if (!SD.exists((channelPath + "/" + base + ".mjpeg").c_str())) {
                  appendHealthIssue(page, "Orphaned audio file", channelName + "/" + filename, mediaPath, "file");
                  issueCount++;
                }
              } else {
                appendHealthIssue(page, "Unsupported file", channelName + "/" + filename, mediaPath, "file");
                issueCount++;
              }
            }
            media.close();
            media = channelDir.openNextFile();
          }
          channelDir.close();
        }
        if (entryCount == 0) {
          bool removable = channelName == "random" || !isNumericChannelName(channelName) || channelName.toInt() == video_count;
          appendHealthIssue(page, "Empty channel folder", channelName,
                            removable ? channelPath : "", removable ? "folder" : "");
          issueCount++;
        }
        if (channelName != "random" && channelVideos > 1) {
          appendHealthIssue(page, "Numbered channel contains multiple videos",
                            channelName + " contains " + String(channelVideos) + " videos; only the first is used.");
          issueCount++;
        }
      }
      channelEntry.close();
      channelEntry = videosRoot.openNextFile();
    }
    videosRoot.close();
  }
  if (!issueCount) page += "<p style='color:#86efac;font-weight:700'>No SD-card organization problems were found.</p>";
  page += "</div><div class='card status'><div class='stat'><div class='label'>Issues</div><div class='value'>" + String(issueCount) + "</div></div>";
  page += "<div class='stat'><div class='label'>Videos</div><div class='value'>" + String(videoFiles) + "</div></div>";
  page += "<div class='stat'><div class='label'>Audio files</div><div class='value'>" + String(audioFiles) + "</div></div></div>";
  page += F("<p class='meta'>Cleanup buttons are shown only for orphaned audio, unsupported files, and folders that can be removed without creating a channel-number gap.</p></main></body></html>");
  if (resumePlayback) playback_paused = false;
  webServer.send(200, "text/html", page);
}

static void handleSDCleanup() {
  String path = webServer.arg("path");
  String action = webServer.arg("action");
  if (!path.startsWith("/Videos/") || path.indexOf("..") >= 0 || path.length() > 255) {
    webServer.send(400, "text/plain", "Invalid SD-card path");
    return;
  }
  bool resumePlayback = !playback_paused;
  playback_paused = true;
  web_stop_requested = true;
  audio_stop_requested = true;
  uint32_t stopStarted = millis();
  while (playback_active && millis() - stopStarted < 4000) vTaskDelay(pdMS_TO_TICKS(10));
  bool removed = false;
  if (action == "file") {
    File item = SD.open(path.c_str());
    bool regularFile = item && !item.isDirectory();
    if (item) item.close();
    String lower = path;
    lower.toLowerCase();
    bool supportedAudio = lower.endsWith(".mp3") || lower.endsWith(".aac");
    bool supportedVideo = lower.endsWith(".mjpeg");
    bool directlyInsideVideos = path.indexOf('/', 8) < 0;
    bool safeToRemove = directlyInsideVideos || !supportedVideo;
    if (supportedAudio && !directlyInsideVideos) {
      int dot = path.lastIndexOf('.');
      safeToRemove = dot > 0 && !SD.exists((path.substring(0, dot) + ".mjpeg").c_str());
    }
    if (regularFile && safeToRemove) removed = SD.remove(path.c_str());
  } else if (action == "folder") {
    File folder = SD.open(path.c_str());
    bool empty = folder && folder.isDirectory();
    if (empty) {
      File first = folder.openNextFile();
      if (first) { empty = false; first.close(); }
    }
    if (folder) folder.close();
    String name = safeFilename(path);
    bool safePosition = name == "random" || !isNumericChannelName(name) || name.toInt() == video_count;
    if (empty && safePosition) {
      removed = SD.rmdir(path.c_str());
      if (removed && name == "random") has_random = false;
      else if (removed && isNumericChannelName(name) && name.toInt() == video_count) video_count--;
    }
  }
  if (!removed) {
    if (resumePlayback) playback_paused = false;
    webServer.send(409, "text/plain", "The item was not removed because it is no longer safe or empty");
    return;
  }
  logEvent("SD cleanup removed %s", path.c_str());
  if (resumePlayback) playback_paused = false;
  webServer.sendHeader("Location", "/sd-health");
  webServer.send(303);
}

static String recentEventsText() {
  String text;
  uint8_t count;
  uint8_t next;
  portENTER_CRITICAL(&eventLogMux);
  count = eventLogCount;
  next = eventLogNext;
  portEXIT_CRITICAL(&eventLogMux);
  for (uint8_t i = 0; i < count; i++) {
    uint8_t index = (next + EVENT_LOG_CAPACITY - count + i) % EVENT_LOG_CAPACITY;
    char message[EVENT_LOG_MESSAGE_SIZE];
    uint32_t timestamp;
    portENTER_CRITICAL(&eventLogMux);
    timestamp = eventLogTimes[index];
    strncpy(message, eventLogMessages[index], sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';
    portEXIT_CRITICAL(&eventLogMux);
    text += "[" + String(timestamp / 1000) + " sec] " + String(message) + "\n";
  }
  if (!count) text = "No events recorded.\n";
  return text;
}

static void appendRecentEventsHtml(String &page) {
  String events = recentEventsText();
  page += "<div class='card'><h2>Recent events</h2><pre style='white-space:pre-wrap;overflow-wrap:anywhere;background:#0f172a;padding:12px;border-radius:8px'>";
  page += htmlEscape(events);
  page += "</pre><a href='/diagnostic-report'><button>Download diagnostic report</button></a></div>";
}

static void sendDiagnosticReport() {
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  String report = "MiniTV diagnostic report\n========================\n";
  report += "Firmware: " + String(MINITV_VERSION) + "\n";
  report += "Device: " + configuredDeviceName + "\n";
  report += "Hostname: " + configuredHostname + ".local\n";
  report += "IP address: " + WiFi.localIP().toString() + "\n";
  report += "MAC address: " + WiFi.macAddress() + "\n";
  report += "Wi-Fi RSSI: " + String(WiFi.RSSI()) + " dBm\n";
  report += "Free heap: " + String(ESP.getFreeHeap()) + " bytes\n";
  report += "Uptime: " + String(millis() / 1000) + " seconds\n";
  report += "Reset reason: " + String((int)esp_reset_reason()) + "\n";
  report += "SD total: " + String((unsigned)(total / 1048576ULL)) + " MB\n";
  report += "SD used: " + String((unsigned)(used / 1048576ULL)) + " MB\n";
  report += "Numbered channels: " + String(video_count) + "\n";
  report += "Random channel: " + String(has_random ? "present" : "absent") + "\n";
  report += "Current channel: " + String(video_idx) + "\n";
  report += "Current title: " + current_video_title + "\n";
  report += "Playback: " + String(playback_active ? "active" : "idle") + "\n";
  report += "Audio task: " + String(audio_task_running ? "running" : "idle") + "\n\nRecent events\n-------------\n";
  report += recentEventsText();
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + configuredHostname + "-diagnostics.txt\"");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/plain", report);
}

static void sendDiagnosticsPage() {
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  String page = pageHeader("MiniTV Diagnostics");
  page += "<h1>Diagnostics</h1><div class='card status'>";
  page += "<div class='stat'><div class='label'>Firmware</div><div class='value'>" + String(MINITV_VERSION) + "</div></div>";
  page += "<div class='stat'><div class='label'>Device name</div><div class='value'>" + htmlEscape(configuredDeviceName) + "</div></div>";
  page += "<div class='stat'><div class='label'>Hostname</div><div class='value'>" + htmlEscape(configuredHostname) + ".local</div></div>";
  page += "<div class='stat'><div class='label'>IP address</div><div class='value'>" + WiFi.localIP().toString() + "</div></div>";
  page += "<div class='stat'><div class='label'>Wi-Fi RSSI</div><div class='value'>" + String(WiFi.RSSI()) + " dBm</div></div>";
  page += "<div class='stat'><div class='label'>Free heap</div><div class='value'>" + String(ESP.getFreeHeap() / 1024) + " KB</div></div>";
  page += "<div class='stat'><div class='label'>Uptime</div><div class='value'>" + String(millis() / 1000) + " sec</div></div>";
  page += "<div class='stat'><div class='label'>Reset reason</div><div class='value'>" + String((int)esp_reset_reason()) + "</div></div>";
  page += "<div class='stat'><div class='label'>SD total</div><div class='value'>" + String((unsigned)(total / 1048576ULL)) + " MB</div></div>";
  page += "<div class='stat'><div class='label'>SD used</div><div class='value'>" + String((unsigned)(used / 1048576ULL)) + " MB</div></div>";
  page += "<div class='stat'><div class='label'>Channels</div><div class='value'>" + String(video_count) + "</div></div>";
  page += "<div class='stat'><div class='label'>Audio task</div><div class='value'>" + String(audio_task_running ? "Running" : "Idle") + "</div></div></div>";
  appendRecentEventsHtml(page);
  page += "<div class='card'><h2>Actions</h2><div class='row'><a class='navlink' href='/'>Back to controls</a><a class='navlink' href='/sd-health'>Open SD health</a>";
  page += "<button class='danger' onclick=\"if(confirm('Restart MiniTV?'))fetch('/restart',{method:'POST'}).then(()=>setTimeout(()=>location.href='/',2500))\">Restart MiniTV</button></div></div></main></body></html>";
  webServer.send(200, "text/html", page);
}

static void handleDeletePair() {
  String channel = safeFilename(webServer.arg("channel"));
  String baseName = safeFilename(webServer.arg("base"));
  bool removeFolder = webServer.hasArg("delete_folder") && webServer.arg("delete_folder") == "1";
  if (channel.length() == 0 || baseName.length() == 0) {
    webServer.send(400, "text/plain", "Invalid channel or filename");
    return;
  }

  playback_paused = true;
  web_stop_requested = true;
  audio_stop_requested = true;
  uint32_t started = millis();
  while (playback_active && millis() - started < 3000) vTaskDelay(pdMS_TO_TICKS(10));

  String prefix = "/Videos/" + channel + "/" + baseName;
  String videoPath = prefix + ".mjpeg";
  String aacPath = prefix + ".aac";
  String mp3Path = prefix + ".mp3";
  bool removedVideo = !SD.exists(videoPath.c_str()) || SD.remove(videoPath.c_str());
  bool removedAAC = !SD.exists(aacPath.c_str()) || SD.remove(aacPath.c_str());
  bool removedMP3 = !SD.exists(mp3Path.c_str()) || SD.remove(mp3Path.c_str());
  bool success = removedVideo && removedAAC && removedMP3;
  String directory = "/Videos/" + channel;
  bool folderRemoved = false;
  if (success && removeFolder) {
    folderRemoved = !SD.exists(directory.c_str()) || SD.rmdir(directory.c_str());
  }

  String page = pageHeader(success ? "Files deleted" : "Delete failed");
  page += success ? F("<h1>Video and matching audio deleted</h1>")
                  : F("<h1>One or more files could not be deleted</h1>");
  page += "<p>" + htmlEscape(baseName) + "</p>";
  if (removeFolder) {
    page += folderRemoved
      ? F("<p>The now-empty channel folder was also removed.</p>")
      : F("<p>The channel folder was retained because it still contains other files.</p>");
  }
  page += "<p>The MiniTV will restart.</p>"
          "<p><a href='/'>Return to MiniTV controls</a></p>"
          "<script>setTimeout(()=>location.href='/',2500)</script></body></html>";
  webServer.send(success ? 200 : 500, "text/html", page);
  requestRestart();
}

static void handleChannelLabel() {
  int channel = webServer.arg("channel").toInt();
  String label = webServer.arg("label");
  label.trim();
  if (channel < 1 || channel > video_count || label.length() > 32) {
    webServer.send(400, "text/plain", "Invalid channel name");
    return;
  }
  Preferences labels;
  labels.begin(APP_NAME, false);
  String key = "ch_" + String(channel);
  if (label.length()) labels.putString(key.c_str(), label);
  else labels.remove(key.c_str());
  labels.end();
  webServer.sendHeader("Location", "/files");
  webServer.send(303);
}

static void handleRenamePair() {
  String channel = safeFilename(webServer.arg("channel"));
  String oldBase = safeFilename(webServer.arg("base"));
  String newBase = safeFilename(webServer.arg("new_base"));
  if (newBase.endsWith(".mjpeg")) newBase.remove(newBase.length() - 6);
  if (newBase.endsWith(".mp3")) newBase.remove(newBase.length() - 4);
  if (!channel.length() || !oldBase.length() || !newBase.length()) {
    webServer.send(400, "text/plain", "Invalid channel or title");
    return;
  }
  if (oldBase == newBase) {
    webServer.sendHeader("Location", "/files");
    webServer.send(303);
    return;
  }

  playback_paused = true;
  web_stop_requested = true;
  audio_stop_requested = true;
  uint32_t started = millis();
  while (playback_active && millis() - started < 4000) vTaskDelay(pdMS_TO_TICKS(10));

  String oldPrefix = "/Videos/" + channel + "/" + oldBase;
  String newPrefix = "/Videos/" + channel + "/" + newBase;
  if (SD.exists((newPrefix + ".mjpeg").c_str()) || SD.exists((newPrefix + ".mp3").c_str())) {
    webServer.send(409, "text/plain", "A video with that title already exists");
    return;
  }
  bool videoOK = SD.rename((oldPrefix + ".mjpeg").c_str(), (newPrefix + ".mjpeg").c_str());
  bool audioExists = SD.exists((oldPrefix + ".mp3").c_str());
  bool audioOK = !audioExists || SD.rename((oldPrefix + ".mp3").c_str(), (newPrefix + ".mp3").c_str());
  if (!audioOK && videoOK) SD.rename((newPrefix + ".mjpeg").c_str(), (oldPrefix + ".mjpeg").c_str());
  bool success = videoOK && audioOK;
  String page = pageHeader(success ? "Title renamed" : "Rename failed");
  page += success ? "<h1>Video and audio renamed</h1>" : "<h1>Rename failed</h1>";
  if (success) playback_paused = false;
  page += "<p>" + htmlEscape(oldBase) + " &rarr; " + htmlEscape(newBase) + "</p>";
  page += success ? "<p>The new title is active and playback will resume.</p>" : "<p>The original files were retained.</p>";
  page += "<p><a href='/'>Return to MiniTV controls</a> &nbsp; <a href='/files'>Return to SD card</a></p>";
  page += "</body></html>";
  webServer.send(success ? 200 : 500, "text/html", page);
}

static void handleSwapChannel() {
  int first = webServer.arg("channel").toInt();
  int second = webServer.arg("target").toInt();
  if (first < 1 || second < 1 || first > video_count || second > video_count) {
    webServer.send(400, "text/plain", "Invalid channel number");
    return;
  }
  if (first == second) {
    webServer.sendHeader("Location", "/files");
    webServer.send(303);
    return;
  }
  playback_paused = true;
  web_stop_requested = true;
  audio_stop_requested = true;
  uint32_t started = millis();
  while (playback_active && millis() - started < 4000) vTaskDelay(pdMS_TO_TICKS(10));

  String firstPath = "/Videos/" + String(first);
  String secondPath = "/Videos/" + String(second);
  String tempPath = "/Videos/__channel_swap__";
  bool movedFirst = !SD.exists(tempPath.c_str()) && SD.rename(firstPath.c_str(), tempPath.c_str());
  bool movedSecond = movedFirst && SD.rename(secondPath.c_str(), firstPath.c_str());
  bool success = movedSecond && SD.rename(tempPath.c_str(), secondPath.c_str());
  if (!success) {
    if (movedSecond) SD.rename(firstPath.c_str(), secondPath.c_str());
    if (movedFirst && SD.exists(tempPath.c_str())) SD.rename(tempPath.c_str(), firstPath.c_str());
  }
  if (success) {
    Preferences labels;
    labels.begin(APP_NAME, false);
    String firstKey = "ch_" + String(first);
    String secondKey = "ch_" + String(second);
    String firstLabel = labels.getString(firstKey.c_str(), "");
    String secondLabel = labels.getString(secondKey.c_str(), "");
    if (secondLabel.length()) labels.putString(firstKey.c_str(), secondLabel); else labels.remove(firstKey.c_str());
    if (firstLabel.length()) labels.putString(secondKey.c_str(), firstLabel); else labels.remove(secondKey.c_str());
    labels.end();
    if (video_idx == first) video_idx = second;
    else if (video_idx == second) video_idx = first;
    preferences.begin(APP_NAME, false);
    preferences.putInt(K_VIDEO_INDEX, video_idx);
    preferences.end();
  }
  String page = pageHeader(success ? "Channels swapped" : "Channel swap failed");
  page += success ? "<h1>Channel numbers swapped</h1>" : "<h1>Channel swap failed</h1>";
  if (success) playback_paused = false;
  page += success ? "<p>The new channel order is active and playback will resume.</p>" : "<p>The original channel order was retained.</p>";
  page += "<p><a href='/'>Return to MiniTV controls</a> &nbsp; <a href='/files'>Return to SD card</a></p>";
  page += "</body></html>";
  webServer.send(success ? 200 : 500, "text/html", page);
}

static void handleAction() {
  String action = webServer.arg("do");
  if (video_count == 0 && !has_random &&
      (action == "play" || action == "next" || action == "prev" || action == "channel")) {
    webServer.send(409, "text/plain", "No videos are installed. Upload a video first.");
    return;
  }
  if (action == "mute") {
    is_muted = !is_muted;
    preferences.begin(APP_NAME, false);
    preferences.putBool(K_MUTE, is_muted);
    preferences.end();
  } else if (action == "play") {
    wakeMiniTV();
  } else if (action == "stop") {
    playback_paused = true;
    web_stop_requested = true;
  } else if (action == "next" || action == "prev" || action == "channel") {
    wakeMiniTV();
    int selected = video_idx;
    // In Random mode, Next/Previous means choose another random video rather
    // than leave Random and jump to channel 1.
    if ((action == "next" || action == "prev") && video_idx == 0) selected = 0;
    else if (action == "next") selected++;
    else if (action == "prev") selected--;
    else selected = webServer.arg("channel").toInt();
    int minimum = has_random ? 0 : 1;
    if (selected < minimum) selected = video_count;
    if (selected > video_count) selected = minimum;
    preferences.begin(APP_NAME, false);
    preferences.putInt(K_VIDEO_INDEX, selected);
    preferences.end();
    video_idx = selected;
    playback_paused = false;
    web_stop_requested = true;
    webServer.sendHeader("Location", "/");
    webServer.send(303);
    return;
  }
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

static void handleVolume() {
  int value = constrain(webServer.arg("value").toInt(), 0, 200);
  audio_volume = value;
  preferences.begin(APP_NAME, false);
  preferences.putInt("volume", value);
  preferences.end();
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

static void handleBrightness() {
  int value = constrain(webServer.arg("value").toInt(), 0, 255);
  screen_brightness = value;
  ledcWrite(0, screen_brightness);
  preferences.begin(APP_NAME, false);
  preferences.putInt("brightness", value);
  preferences.end();
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

static void handlePrepareUpload() {
  Serial.println("Preparing upload: stopping video, audio, and amplifier");
  logEvent("SD media upload preparation started");
  playback_paused = true;
  web_stop_requested = true;
  audio_stop_requested = true;
#ifdef AUDIO_ENABLE_PIN
  digitalWrite(AUDIO_ENABLE_PIN, HIGH); // Active-low amplifier: remove speaker load
#endif

  uint32_t started = millis();
  while (audio_task_running && millis() - started < 1500) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (audio_task_handle != NULL) {
    if (audio_task_running) Serial.println("Audio task did not stop normally; terminating it for upload");
    vTaskDelete(audio_task_handle);
    audio_task_handle = NULL;
    audio_task_running = false;
  }
  i2s_stop(I2S_NUM_0);

  started = millis();
  while (playback_active && millis() - started < 4500) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  bool ready = !audio_task_running && !playback_active;
  Serial.printf("Upload preparation %s (audio=%d, video=%d)\n",
                ready ? "complete" : "timed out",
                (int)audio_task_running, (int)playback_active);
  webServer.sendHeader("Connection", "close");
  webServer.send(ready ? 200 : 503, "text/plain", ready ? "Ready" : "Playback did not stop");
}

static void handleUploadData() {
  HTTPUpload &upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (!uploadBatchActive) {
      uploadBatchActive = true;
      uploadOK = true;
    }
    playback_paused = true;
    web_stop_requested = true;
    audio_stop_requested = true;
    uint32_t started = millis();
    while (audio_task_running && millis() - started < 1500) vTaskDelay(pdMS_TO_TICKS(10));

    String channel = webServer.arg("channel");
    if (channel.length() == 0) channel = "1";
    channel = safeFilename(channel);
    String directory = "/Videos/" + channel;
    if (!SD.exists(directory)) SD.mkdir(directory);
    String nextPath = directory + "/" + safeFilename(upload.filename);
    uploadIsChunked = webServer.arg("chunked") == "1";
    uploadChunkOffset = uploadIsChunked ? (size_t)strtoull(webServer.arg("offset").c_str(), NULL, 10) : 0;
    bool reuseOpenFile = uploadFile && nextPath == uploadPath && uploadFile.size() == uploadChunkOffset;
    if (!reuseOpenFile) {
      if (uploadFile) {
        uploadFile.flush();
        uploadFile.close();
      }
      uploadPath = nextPath;
      if (uploadChunkOffset == 0 && SD.exists(uploadPath.c_str())) SD.remove(uploadPath.c_str());
      size_t existingSize = 0;
      if (uploadChunkOffset > 0) {
        File existing = SD.open(uploadPath.c_str(), FILE_READ);
        existingSize = existing ? existing.size() : 0;
        if (existing) existing.close();
      }
      bool offsetValid = uploadChunkOffset == 0 || existingSize == uploadChunkOffset;
      if (!offsetValid) Serial.printf("Upload offset mismatch: browser %u, SD %u\n", (unsigned)uploadChunkOffset, (unsigned)existingSize);
      uploadFile = offsetValid ? SD.open(uploadPath.c_str(), uploadChunkOffset > 0 ? FILE_APPEND : FILE_WRITE) : File();
    }
    uploadCurrentFileOK = (bool)uploadFile;
    if (!uploadCurrentFileOK) uploadOK = false;
    if (uploadChunkOffset == 0) Serial.printf("Upload file starting: %s\n", uploadPath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!uploadCurrentFileOK) return;
    size_t offset = 0;
    uint32_t writeStarted = millis();
    while (uploadFile && offset < upload.currentSize && millis() - writeStarted < 3000) {
      size_t written = uploadFile.write(upload.buf + offset, upload.currentSize - offset);
      if (written > 0) {
        offset += written;
        writeStarted = millis();
      } else {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }
    if (offset != upload.currentSize) {
      uploadCurrentFileOK = false;
      uploadOK = false;
      Serial.printf("Upload chunk write failed: wanted %u, wrote %u\n",
                    upload.currentSize, (unsigned)offset);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.flush();
    }
    size_t storedSize = uploadFile ? uploadFile.size() : 0;
    uploadStoredSize = storedSize;
    size_t expectedSize = uploadChunkOffset + upload.totalSize;
    if (storedSize != expectedSize) {
      uploadCurrentFileOK = false;
      uploadOK = false;
    }
    if (storedSize != expectedSize) {
      Serial.printf("Upload block error: %s, expected %u bytes, stored %u bytes, SIZE MISMATCH\n",
                    uploadPath.c_str(), (unsigned)expectedSize, (unsigned)storedSize);
    } else if ((storedSize % 1048576UL) < 32768UL) {
      Serial.printf("Upload progress: %s - %u MB verified\n",
                    uploadPath.c_str(), (unsigned)(storedSize / 1048576UL));
    }
    if (storedSize != expectedSize) {
      // A failed SD write leaves the current FAT file handle unusable. Close
      // it but retain all verified bytes; the next request reopens at this
      // exact position and retries the missing data.
      if (uploadFile) uploadFile.close();
      Serial.println(storedSize > uploadChunkOffset
                       ? "Partial block retained; file closed for resume"
                       : "No block progress; file closed for retry");
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) {
      uploadFile.flush();
      uploadStoredSize = uploadFile.size();
    }
    uploadOK = false;
  }
}

static void handleUploadComplete() {
  bool finalBlock = !uploadIsChunked || webServer.arg("final") == "1";
  if (uploadIsChunked && !finalBlock) {
    bool madeProgress = uploadStoredSize > uploadChunkOffset;
    webServer.sendHeader("Connection", "close");
    webServer.sendHeader("Cache-Control", "no-store");
    webServer.send(madeProgress ? 200 : 500, "text/plain", String((unsigned)uploadStoredSize));
    if (!madeProgress) Serial.printf("Upload block acknowledged at byte %u: FAILED\n", (unsigned)uploadStoredSize);
    uploadBatchActive = false;
    return;
  }
  String page = pageHeader(uploadOK ? "Upload complete" : "Upload failed");
  page += uploadOK ? F("<h1>Upload complete</h1><p>The MiniTV will restart and resume playback.</p>")
                   : F("<h1>Upload failed</h1><p>The incomplete file was removed. The MiniTV will restart so you can try again.</p>");
  page += F("<p><a href='/'>Return to MiniTV</a></p></body></html>");
  webServer.send(uploadOK ? 200 : 500, "text/html", page);
  requestRestart();
  uploadBatchActive = false;
}

static void handleUploadSize() {
  String channel = safeFilename(webServer.arg("channel"));
  String filename = safeFilename(webServer.arg("file"));
  String path = "/Videos/" + channel + "/" + filename;
  size_t size = 0;
  if (uploadFile && path == uploadPath) {
    uploadFile.flush();
    size = uploadFile.size();
  } else {
    File file = SD.open(path.c_str(), FILE_READ);
    size = file ? file.size() : 0;
    if (file) file.close();
  }
  webServer.sendHeader("Connection", "close");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/plain", String((unsigned)size));
}

static void handleStorage() {
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  uint64_t freeBytes = total > used ? total - used : 0;
  String json = "{\"totalKB\":" + String((unsigned long)(total / 1024ULL)) +
                ",\"usedKB\":" + String((unsigned long)(used / 1024ULL)) +
                ",\"freeKB\":" + String((unsigned long)(freeBytes / 1024ULL)) + "}";
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", json);
}

static void handleFileExists() {
  String channel = safeFilename(webServer.arg("channel"));
  String filename = safeFilename(webServer.arg("file"));
  String path = "/Videos/" + channel + "/" + filename;
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/plain", SD.exists(path.c_str()) ? "1" : "0");
}

static void handleUploadFinish() {
  if (uploadFile) {
    uploadFile.flush();
    uploadFile.close();
  }
  webServer.sendHeader("Connection", "close");
  webServer.send(200, "text/plain", "Upload complete");
  Serial.println("All upload files complete; restarting MiniTV");
  logEvent("SD media upload completed");
  requestRestart();
  uploadBatchActive = false;
}

static void handleCancelUpload() {
  uploadCancelRequested = true;
  if (uploadFile) {
    uploadFile.flush();
    uploadFile.close();
  }
  if (uploadPath.length()) {
    int dot = uploadPath.lastIndexOf('.');
    String base = dot > 0 ? uploadPath.substring(0, dot) : uploadPath;
    String videoPath = base + ".mjpeg";
    String audioPath = base + ".mp3";
    if (SD.exists(videoPath.c_str())) SD.remove(videoPath.c_str());
    if (SD.exists(audioPath.c_str())) SD.remove(audioPath.c_str());
    Serial.printf("Upload cancelled; incomplete pair removed: %s\n", base.c_str());
    logEvent("Media upload cancelled: %s", base.c_str());
  }
  webServer.sendHeader("Connection", "close");
  webServer.send(200, "text/plain", "Upload cancelled");
  requestRestart();
}

static void serviceWebControl() {
  if (!screen_sleeping && sleep_deadline_ms &&
      (int32_t)(millis() - sleep_deadline_ms) >= 0) {
    sleepMiniTV();
  }
  time_t now = time(nullptr);
  if (dailyScheduleEnabled && now > 1700000000 && now / 60 != lastScheduleMinute) {
    lastScheduleMinute = now / 60;
    struct tm localNow;
    if (localtime_r(&now, &localNow)) {
      char clockText[6];
      strftime(clockText, sizeof(clockText), "%H:%M", &localNow);
      String currentTime(clockText);
      if (currentTime == dailySleepTime && !screen_sleeping) sleepMiniTV();
      else if (currentTime == dailyWakeTime && screen_sleeping) wakeMiniTV();
    }
  }
  wl_status_t status = WiFi.status();
  if (status != lastWiFiStatus) {
    Serial.printf("Wi-Fi status changed: %d", (int)status);
    if (status == WL_CONNECTED) {
      Serial.printf(", IP: %s, RSSI: %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    Serial.println();
    lastWiFiStatus = status;
  }
  if (setupModeActive) setupDnsServer.processNextRequest();
  if (status == WL_CONNECTED || setupModeActive) {
    webServer.handleClient();
  }
  if (restartAfterResponse && millis() - restartRequestedAt > 750) ESP.restart();
}

static bool connectMiniTVWiFi(const String &ssid, const String &password) {
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.printf("Connecting to Wi-Fi %s", ssid.c_str());
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

static void webServerTask(void *) {
  Serial.println("Web server task running");
  while (true) {
    serviceWebControl();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

static bool startWebControl() {
  preferences.begin(APP_NAME, true);
  configuredSSID = preferences.getString("wifi_ssid", WIFI_SSID);
  configuredPassword = preferences.getString("wifi_pass", WIFI_PASSWORD);
  configuredDeviceName = preferences.getString("device_name", "MiniTV");
  configuredHostname = preferences.getString("hostname", WIFI_HOSTNAME);
  configuredAdminPassword = preferences.getString("admin_pass", "");
  configuredTimezone = preferences.getString("timezone", "CST6CDT,M3.2.0,M11.1.0");
  dailyScheduleEnabled = preferences.getBool("sched_en", false);
  dailySleepTime = preferences.getString("sleep_h", "23:00");
  dailyWakeTime = preferences.getString("wake_h", "08:00");
  preferences.end();
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.onEvent(miniTVWiFiEvent);
  WiFi.setHostname(configuredHostname.c_str());
  bool hasConfiguredNetwork = configuredSSID.length() && configuredSSID != "CHANGE_ME";
  bool connected = hasConfiguredNetwork && connectMiniTVWiFi(configuredSSID, configuredPassword);
  if (!connected && configuredSSID != String(WIFI_SSID) && strcmp(WIFI_SSID, "CHANGE_ME")) {
    Serial.println("Saved Wi-Fi failed; trying the network compiled into config.h");
    WiFi.disconnect(true);
    delay(250);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(configuredHostname.c_str());
    configuredSSID = WIFI_SSID;
    configuredPassword = WIFI_PASSWORD;
    connected = connectMiniTVWiFi(configuredSSID, configuredPassword);
    if (connected) {
      preferences.begin(APP_NAME, false);
      preferences.remove("wifi_ssid");
      preferences.remove("wifi_pass");
      preferences.end();
    }
  }
  if (!connected) {
    Serial.println("Normal Wi-Fi unavailable; starting first-run setup mode");
    if (!startWiFiSetupMode()) {
      WiFi.mode(WIFI_OFF);
      return false;
    }
  }

  if (connected) {
    lastWiFiStatus = WL_CONNECTED;
    configTzTime(configuredTimezone.c_str(), "pool.ntp.org", "time.nist.gov");
    Serial.printf("MiniTV MAC address: %s, RSSI: %d dBm\n", WiFi.macAddress().c_str(), WiFi.RSSI());
    MDNS.begin(configuredHostname.c_str());
    MDNS.addService("http", "tcp", 80);
  }
#define SECURE_ROUTE(path, method, handler) webServer.on(path, method, []() { if (requireAdmin()) handler(); })
  webServer.on("/", HTTP_GET, []() {
    if (setupModeActive) sendWiFiSetupPage();
    else if (requireAdmin()) sendHomePage();
  });
  webServer.on("/wifi-setup-save", HTTP_POST, handleWiFiSetupSave);
  SECURE_ROUTE("/upload-page", HTTP_GET, sendHomePage);
  SECURE_ROUTE("/status", HTTP_GET, sendStatus);
  SECURE_ROUTE("/files", HTTP_GET, sendFilesPage);
  SECURE_ROUTE("/sd-health", HTTP_GET, sendSDHealthPage);
  SECURE_ROUTE("/sd-cleanup", HTTP_POST, handleSDCleanup);
  SECURE_ROUTE("/settings", HTTP_GET, sendSettingsPage);
  SECURE_ROUTE("/backup", HTTP_GET, sendBackupPage);
  SECURE_ROUTE("/settings-backup", HTTP_GET, sendSettingsBackup);
  SECURE_ROUTE("/firmware", HTTP_GET, sendFirmwarePage);
  SECURE_ROUTE("/diagnostics", HTTP_GET, sendDiagnosticsPage);
  SECURE_ROUTE("/diagnostic-report", HTTP_GET, sendDiagnosticReport);
  SECURE_ROUTE("/delete", HTTP_POST, handleDeletePair);
  SECURE_ROUTE("/channel-label", HTTP_POST, handleChannelLabel);
  SECURE_ROUTE("/rename", HTTP_POST, handleRenamePair);
  SECURE_ROUTE("/swap-channel", HTTP_POST, handleSwapChannel);
  SECURE_ROUTE("/action", HTTP_GET, handleAction);
  SECURE_ROUTE("/volume", HTTP_GET, handleVolume);
  SECURE_ROUTE("/brightness", HTTP_GET, handleBrightness);
  SECURE_ROUTE("/sleep", HTTP_POST, handleSleepTimer);
  SECURE_ROUTE("/wake", HTTP_POST, handleWake);
  SECURE_ROUTE("/wifi-settings", HTTP_POST, handleWiFiSettings);
  SECURE_ROUTE("/identity-settings", HTTP_POST, handleIdentitySettings);
  SECURE_ROUTE("/admin-settings", HTTP_POST, handleAdminSettings);
  SECURE_ROUTE("/schedule-settings", HTTP_POST, handleScheduleSettings);
  webServer.on("/settings-restore", HTTP_POST,
               []() { if (requireAdmin()) handleRestoreComplete(); },
               []() { if (adminAuthenticated()) handleRestoreData(); });
  webServer.on("/firmware-update", HTTP_POST,
               []() { if (requireAdmin()) handleFirmwareComplete(); },
               []() { if (adminAuthenticated()) handleFirmwareData(); });
  SECURE_ROUTE("/prepare-upload", HTTP_POST, handlePrepareUpload);
  SECURE_ROUTE("/cancel-upload", HTTP_POST, handleCancelUpload);
  SECURE_ROUTE("/upload-size", HTTP_GET, handleUploadSize);
  SECURE_ROUTE("/storage", HTTP_GET, handleStorage);
  SECURE_ROUTE("/file-exists", HTTP_GET, handleFileExists);
  SECURE_ROUTE("/upload-finish", HTTP_POST, handleUploadFinish);
  webServer.on("/restart", HTTP_POST, []() {
    if (!requireAdmin()) return;
    webServer.send(200, "text/plain", "Restarting");
    requestRestart();
  });
  webServer.on("/upload", HTTP_POST,
               []() { if (requireAdmin()) handleUploadComplete(); },
               []() { if (adminAuthenticated()) handleUploadData(); });
  webServer.onNotFound([]() {
    if (setupModeActive) {
      webServer.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
      webServer.send(302, "text/plain", "Open MiniTV Wi-Fi setup");
      return;
    }
    if (!requireAdmin()) return;
    webServer.send(404, "text/plain", "Not found");
  });
#undef SECURE_ROUTE
  webServer.begin();
  if (setupModeActive) Serial.printf("MiniTV Wi-Fi setup: http://%s\n", WiFi.softAPIP().toString().c_str());
  else Serial.printf("%s web control: http://%s.local or http://%s\n", configuredDeviceName.c_str(), configuredHostname.c_str(), WiFi.localIP().toString().c_str());
  BaseType_t taskResult = xTaskCreatePinnedToCore(
    webServerTask, "MiniTV Web", 4096, NULL, 3, NULL, 0);
  if (taskResult != pdPASS) {
    Serial.printf("ERROR: Web server task failed to start: %d\n", (int)taskResult);
    return false;
  }
  Serial.println("Web server task created successfully");
  return true;
}
