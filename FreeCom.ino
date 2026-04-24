/*
  ESP32 - RYLR Secure Web Chat

  ----------------------------
  Features:
  - AES-256-GCM encrypted LoRa messages
  - replay protection
  - Wi-Fi AP password
  - HTTP Basic Auth
  - consent screen
  - rotating feminist micro-manifesto
  - hidden Solidarity Mode
  - hidden /zine page
  - RAM-only logs

  Upload to both ESP32 boards.
  Change only:
    #define NODE_IS_A 1   on the first board
    #define NODE_IS_A 0   on the second board

  Also change these secrets on BOTH boards:
    AP_PASSWORD
    WEB_USER
    WEB_PASS
    AES_KEY
*/

#include <WiFi.h>
#include <WebServer.h>
#include <mbedtls/gcm.h>
#include <mbedtls/base64.h>
#include <esp_system.h>

#define NODE_IS_A 0   // <-- set to 0 on the second board

#if NODE_IS_A
  #define DEVICE_NAME "NodeA"
  #define AP_SSID     "NodeA"
  const uint16_t DEVICE_ADDR = 1;
  const uint16_t REMOTE_ADDR = 2;
#else
  #define DEVICE_NAME "NodeB"
  #define AP_SSID     "NodeB"
  const uint16_t DEVICE_ADDR = 2;
  const uint16_t REMOTE_ADDR = 1;
#endif

// -----------------------------
// Shared secrets: CHANGE THESE
// -----------------------------
static const char* AP_PASSWORD = "WGS2026";
static const char* WEB_USER    = "WGS";
static const char* WEB_PASS    = "password";

// Must match on BOTH boards. Replace with your own random 32 bytes.
static const uint8_t AES_KEY[32] = {
  0x4A, 0xC9, 0x31, 0x7D, 0xE2, 0x10, 0x55, 0xA8,
  0x9B, 0x6E, 0x22, 0x14, 0xF0, 0xCD, 0x87, 0x39,
  0x0E, 0xD7, 0x5C, 0x61, 0x2A, 0x93, 0xB4, 0x78,
  0x1F, 0x44, 0xAE, 0x68, 0x33, 0xB1, 0x09, 0xFE
};

// -----------------------------
// LoRa / UART config
// -----------------------------
const uint32_t LORA_BAUD = 115200;
const uint32_t LORA_BAND = 915000000;
const uint8_t  LORA_NETWORK_ID = 5;

// ESP32 UART2 pins
const int PIN_LORA_RX = 16;   // ESP32 RX <- LoRa TXD
const int PIN_LORA_TX = 17;   // ESP32 TX -> LoRa RXD

// -----------------------------
// Limits
// -----------------------------
const size_t MAX_TEXT_LEN = 96;
const size_t MAX_LOG_ENTRIES = 40;
const int NO_SIGNAL = 32767;
const size_t MAX_LORA_AT_DATA_LEN = 230;

const uint8_t SECURE_PACKET_VERSION = 1;
const size_t HEADER_LEN = 11;
const size_t IV_LEN = 12;
const size_t TAG_LEN = 16;

HardwareSerial LoRaSerial(2);
WebServer server(80);

struct ChatEntry {
  String dir;
  uint16_t peer;
  int rssi;
  int snr;
  uint32_t ms;
  String text;
};

ChatEntry logEntries[MAX_LOG_ENTRIES];
size_t logCount = 0;

String serialLine;
bool loraReady = false;
String statusLine = "Booting...";

bool haveReplayState = false;
uint32_t lastPeerBootId = 0;
uint32_t lastPeerCounter = 0;

uint32_t bootId = 0;
uint32_t txCounter = 0;

// -----------------------------
// HTML
// -----------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>%NODE%</title>
  <style>
    :root {
      --bg: #0f172a;
      --card: #111827;
      --panel: #020617;
      --border: #334155;
      --text: #e2e8f0;
      --muted: #94a3b8;
      --tx: #1d4ed8;
      --rx: #14532d;
      --sys: #3f3f46;
      --send: #22c55e;
      --sendText: #052e16;
      --clear: #f59e0b;
      --clearText: #3b2000;
      --accent1: #be185d;
      --accent2: #7c3aed;
      --accent3: #f59e0b;
      --solidBorder: #f9a8d4;
    }

    * { box-sizing: border-box; }

    body {
      font-family: Arial, sans-serif;
      margin: 0;
      background: var(--bg);
      color: var(--text);
      transition: background 0.3s ease;
    }

    body.solidarity {
      background: linear-gradient(135deg, #240046, #7b2cbf, #c9184a, #ff9e00);
      background-attachment: fixed;
    }

    .wrap {
      max-width: 760px;
      margin: 0 auto;
      padding: 16px;
    }

    .card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 14px;
      margin-bottom: 14px;
      transition: border-color 0.3s ease, box-shadow 0.3s ease;
    }

    body.solidarity .card {
      border-color: var(--solidBorder);
      box-shadow: 0 0 18px rgba(249, 168, 212, 0.18);
    }

    h1 {
      font-size: 1.25rem;
      margin: 0 0 8px 0;
    }

    .small {
      color: var(--muted);
      font-size: 0.92rem;
      line-height: 1.45;
    }

    #status, #manifesto, #collectiveNote {
      margin-top: 8px;
      color: #cbd5e1;
      font-size: 0.92rem;
      line-height: 1.4;
    }

    #easterEgg {
      display: none;
      margin-top: 12px;
      padding: 12px;
      border-radius: 10px;
      background: rgba(255,255,255,0.08);
      color: #fff1f2;
      line-height: 1.5;
      border: 1px solid rgba(255,255,255,0.14);
    }

    #secretNav {
      display: none;
      margin-top: 10px;
      font-size: 0.92rem;
    }

    #secretNav a {
      color: #fde68a;
      text-decoration: none;
      border-bottom: 1px dotted rgba(253, 230, 138, 0.7);
    }

    #log {
      height: 48vh;
      overflow-y: auto;
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 10px;
    }

    .msg {
      margin-bottom: 10px;
      padding: 10px;
      border-radius: 10px;
      word-wrap: break-word;
      white-space: pre-wrap;
    }

    .tx { background: var(--tx); color: white; }
    .rx { background: var(--rx); color: white; }
    .sys { background: var(--sys); color: white; }

    .meta {
      font-size: 0.8rem;
      opacity: 0.88;
      margin-bottom: 4px;
    }

    .row {
      display: flex;
      gap: 8px;
      margin-top: 12px;
    }

    input {
      flex: 1;
      padding: 12px;
      border-radius: 10px;
      border: 1px solid #475569;
      background: #0f172a;
      color: #e2e8f0;
      outline: none;
      font-size: 1rem;
    }

    button {
      padding: 12px 16px;
      border-radius: 10px;
      border: none;
      font-weight: bold;
      font-size: 0.95rem;
      cursor: pointer;
    }

    .sendBtn {
      background: var(--send);
      color: var(--sendText);
    }

    .clearBtn {
      background: var(--clear);
      color: var(--clearText);
    }

    .footerNote {
      margin-top: 10px;
      font-size: 0.82rem;
      color: #94a3b8;
      text-align: center;
    }

    #consentBox {
      position: fixed;
      inset: 0;
      background: rgba(0,0,0,0.82);
      display: flex;
      align-items: center;
      justify-content: center;
      z-index: 9999;
      padding: 16px;
    }

    .consentCard {
      max-width: 460px;
      background: #111827;
      color: #e5e7eb;
      padding: 20px;
      border-radius: 14px;
      border: 1px solid #475569;
      box-shadow: 0 12px 40px rgba(0,0,0,0.45);
    }

    .consentCard h3 {
      margin-top: 0;
      margin-bottom: 10px;
    }

    .consentCard p {
      line-height: 1.5;
      color: #cbd5e1;
    }

    .consentCard button {
      background: #22c55e;
      color: #052e16;
      margin-top: 10px;
      width: 100%;
    }
  </style>
</head>
<body>
  <div id="consentBox">
    <div class="consentCard">
      <h3>Before entering</h3>
      <p>Please respect privacy, consent, and the safety of anyone using this device. Do not copy or share messages without permission.</p>
      <button onclick="enterApp()">I understand</button>
    </div>
  </div>

  <div class="wrap">
    <div class="card">
      <h1>%NODE%</h1>
      <div class="small">
        Wi-Fi SSID: <b>%SSID%</b><br>
        Open: <b>http://%IP%/</b><br>
        LoRa address: <b>%ADDR%</b> → remote: <b>%REMOTE%</b><br>
        Link: <b>Encrypted AES-256-GCM</b>
      </div>

      <div id="status">Loading...</div>
      <div id="manifesto"></div>

      <div id="easterEgg">
        <b>Solidarity mode enabled ✨</b><br>
        Care is infrastructure.<br>
        Consent belongs in design.<br>
        Safer spaces are built deliberately.
      </div>

      <div id="secretNav">
        <a href="/zine">Open field notes</a>
      </div>
    </div>

    <div id="log" class="card"></div>

    <div class="row">
      <input id="msg" maxlength="96" placeholder="Type message and press Enter">
      <button class="sendBtn" onclick="sendMsg()">Send</button>
      <button class="clearBtn" onclick="clearLog()">Clear</button>
    </div>

    <div id="collectiveNote"></div>
    <div class="footerNote">Private local link. Logs clear on reboot.</div>
  </div>

  <script>
    let consentAccepted = false;
    let secretBuffer = "";
    let solidarityMode = false;

    const feministWords = ["solidarity", "care", "consent", "intersectionality"];
    const manifestoLines = [
      "Access is part of justice.",
      "Care work is real work.",
      "Consent belongs in design.",
      "Safety should be built in, not an after thought.",
      "Community begins with listening.",
      "Equity requires action."
    ];
    let manifestoIndex = 0;

    function escapeHtml(s) {
      return s.replace(/[&<>"']/g, function(c) {
        return {
          '&': '&amp;',
          '<': '&lt;',
          '>': '&gt;',
          '"': '&quot;',
          "'": '&#39;'
        }[c];
      });
    }

    function enterApp() {
      consentAccepted = true;
      document.getElementById("consentBox").style.display = "none";
      sessionStorage.setItem("consentAccepted", "1");
      refreshLog();
    }

    function rotateManifesto() {
      document.getElementById("manifesto").textContent = manifestoLines[manifestoIndex];
      manifestoIndex = (manifestoIndex + 1) % manifestoLines.length;
    }

    function setSolidarityMode(enabled) {
      solidarityMode = enabled;
      document.body.classList.toggle("solidarity", enabled);
      document.getElementById("easterEgg").style.display = enabled ? "block" : "none";
      document.getElementById("secretNav").style.display = enabled ? "block" : "none";
    }

    function maybeShowCollectiveNote(count) {
      const note = document.getElementById("collectiveNote");
      if (count >= 8) {
        note.textContent = "Collective communication takes care, listening, and trust.";
      } else {
        note.textContent = "";
      }
    }

    async function refreshLog() {
      if (!consentAccepted) return;

      try {
        const r = await fetch('/messages?_=' + Date.now(), { cache: 'no-store' });
        const data = await r.json();

        document.getElementById('status').textContent = data.status;

        const log = document.getElementById('log');
        log.innerHTML = data.items.map(item => {
          const cls = item.dir === 'RX' ? 'rx' : (item.dir === 'TX' ? 'tx' : 'sys');
          const sig = (item.rssi !== null && item.snr !== null)
            ? ` · RSSI ${item.rssi} · SNR ${item.snr}`
            : '';
          return `
            <div class="msg ${cls}">
              <div class="meta">${item.dir} ${item.peer ? ('peer ' + item.peer) : ''} · ${item.t}${sig}</div>
              <div>${escapeHtml(item.text)}</div>
            </div>
          `;
        }).join('');

        maybeShowCollectiveNote(data.items.length);
        log.scrollTop = log.scrollHeight;
      } catch (e) {
        document.getElementById('status').textContent = 'Could not fetch messages';
      }
    }

    async function sendMsg() {
      if (!consentAccepted) return;

      const el = document.getElementById('msg');
      const msg = el.value.trim();
      if (!msg) return;

      el.value = '';

      try {
        const r = await fetch('/send', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
          body: 'm=' + encodeURIComponent(msg)
        });
        const text = await r.text();
        if (!r.ok) {
          alert(text);
        }
      } catch (e) {
        alert('Send failed');
      }

      refreshLog();
    }

    async function clearLog() {
      if (!consentAccepted) return;

      try {
        await fetch('/clear', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
          body: 'x=1'
        });
      } catch (e) {}

      refreshLog();
    }

    document.addEventListener("keydown", function(e) {
      if (e.key.length === 1) {
        secretBuffer += e.key.toLowerCase();
        if (secretBuffer.length > 48) {
          secretBuffer = secretBuffer.slice(-48);
        }

        for (const word of feministWords) {
          if (secretBuffer.includes(word)) {
            setSolidarityMode(!solidarityMode);
            secretBuffer = "";
            break;
          }
        }
      }
    });

    document.getElementById('msg').addEventListener('keydown', function(e) {
      if (e.key === 'Enter') sendMsg();
    });

    if (sessionStorage.getItem("consentAccepted") === "1") {
      consentAccepted = true;
      document.getElementById("consentBox").style.display = "none";
    }

    rotateManifesto();
    setInterval(rotateManifesto, 5000);
    refreshLog();
    setInterval(refreshLog, 1200);
  </script>
</body>
</html>
)rawliteral";

// -----------------------------
// Helpers
// -----------------------------
String sanitizeMessage(String msg) {
  msg.replace("\r", " ");
  msg.replace("\n", " ");
  msg.trim();
  if (msg.length() > MAX_TEXT_LEN) {
    msg = msg.substring(0, MAX_TEXT_LEN);
  }
  return msg;
}

String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 16);

  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '\"': out += "\\\""; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) out += ' ';
        else out += c;
    }
  }
  return out;
}

String hmsFromMillis(uint32_t ms) {
  uint32_t sec = ms / 1000;
  uint32_t h = sec / 3600;
  uint32_t m = (sec % 3600) / 60;
  uint32_t s = sec % 60;

  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
           (unsigned long)h, (unsigned long)m, (unsigned long)s);
  return String(buf);
}

void addLog(const String& dir, uint16_t peer, const String& text, int rssi = NO_SIGNAL, int snr = NO_SIGNAL) {
  if (logCount < MAX_LOG_ENTRIES) {
    logEntries[logCount++] = {dir, peer, rssi, snr, millis(), text};
  } else {
    for (size_t i = 1; i < MAX_LOG_ENTRIES; ++i) {
      logEntries[i - 1] = logEntries[i];
    }
    logEntries[MAX_LOG_ENTRIES - 1] = {dir, peer, rssi, snr, millis(), text};
  }

  Serial.printf("[%s] peer=%u rssi=%d snr=%d text=%s\n",
                dir.c_str(), peer, rssi, snr, text.c_str());
}

void clearLogs() {
  logCount = 0;
  addLog("SYS", 0, "Log cleared");
}

String buildMessagesJson() {
  String json;
  json.reserve(4096);

  json += "{";
  json += "\"status\":\"" + jsonEscape(statusLine) + "\",";
  json += "\"items\":[";

  for (size_t i = 0; i < logCount; ++i) {
    if (i) json += ",";

    json += "{";
    json += "\"dir\":\"" + jsonEscape(logEntries[i].dir) + "\",";
    json += "\"peer\":" + String(logEntries[i].peer) + ",";
    json += "\"rssi\":";
    if (logEntries[i].rssi == NO_SIGNAL) json += "null";
    else json += String(logEntries[i].rssi);
    json += ",";
    json += "\"snr\":";
    if (logEntries[i].snr == NO_SIGNAL) json += "null";
    else json += String(logEntries[i].snr);
    json += ",";
    json += "\"t\":\"" + hmsFromMillis(logEntries[i].ms) + "\",";
    json += "\"text\":\"" + jsonEscape(logEntries[i].text) + "\"";
    json += "}";
  }

  json += "]";
  json += "}";
  return json;
}

void putU32BE(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)((v >> 24) & 0xFF);
  p[1] = (uint8_t)((v >> 16) & 0xFF);
  p[2] = (uint8_t)((v >> 8) & 0xFF);
  p[3] = (uint8_t)(v & 0xFF);
}

uint32_t getU32BE(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) |
         ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8)  |
         ((uint32_t)p[3]);
}

void fillRandom(uint8_t* dst, size_t len) {
  size_t i = 0;
  while (i < len) {
    uint32_t r = esp_random();
    for (int k = 0; k < 4 && i < len; ++k) {
      dst[i++] = (uint8_t)((r >> (k * 8)) & 0xFF);
    }
  }
}

size_t projectedEncodedLen(size_t plainLen) {
  size_t rawLen = HEADER_LEN + IV_LEN + plainLen + TAG_LEN;
  return 4 * ((rawLen + 2) / 3);
}

bool base64EncodeBuf(const uint8_t* data, size_t len, String& out) {
  size_t outCap = 4 * ((len + 2) / 3) + 4;
  unsigned char* buf = (unsigned char*)malloc(outCap);
  if (!buf) return false;

  size_t outLen = 0;
  int rc = mbedtls_base64_encode(buf, outCap, &outLen, data, len);
  if (rc != 0) {
    free(buf);
    return false;
  }

  buf[outLen] = 0;
  out = String((char*)buf);
  free(buf);
  return true;
}

bool base64DecodeBuf(const String& in, uint8_t*& out, size_t& outLen) {
  size_t outCap = ((in.length() + 3) / 4) * 3 + 4;
  out = (uint8_t*)malloc(outCap);
  if (!out) return false;

  int rc = mbedtls_base64_decode(out, outCap, &outLen,
                                 (const unsigned char*)in.c_str(), in.length());
  if (rc != 0) {
    free(out);
    out = nullptr;
    outLen = 0;
    return false;
  }

  return true;
}

// -----------------------------
// Encryption
// -----------------------------
bool encryptMessageToBase64(const String& plaintext, String& outB64, String& err) {
  String msg = sanitizeMessage(plaintext);
  const size_t ptLen = msg.length();

  if (ptLen == 0) {
    err = "Empty message";
    return false;
  }

  if (ptLen > MAX_TEXT_LEN) {
    err = "Message too long";
    return false;
  }

  if (projectedEncodedLen(ptLen) > MAX_LORA_AT_DATA_LEN) {
    err = "Encrypted packet too large";
    return false;
  }

  size_t rawLen = HEADER_LEN + IV_LEN + ptLen + TAG_LEN;
  uint8_t* packet = (uint8_t*)malloc(rawLen);
  if (!packet) {
    err = "Out of memory";
    return false;
  }

  uint8_t* header = packet;
  uint8_t* iv = packet + HEADER_LEN;
  uint8_t* ciphertext = packet + HEADER_LEN + IV_LEN;
  uint8_t* tag = packet + HEADER_LEN + IV_LEN + ptLen;

  header[0] = SECURE_PACKET_VERSION;
  header[1] = (uint8_t)((DEVICE_ADDR >> 8) & 0xFF);
  header[2] = (uint8_t)(DEVICE_ADDR & 0xFF);
  putU32BE(header + 3, bootId);

  uint32_t counter = ++txCounter;
  putU32BE(header + 7, counter);

  iv[0] = header[1];
  iv[1] = header[2];
  iv[2] = header[7];
  iv[3] = header[8];
  iv[4] = header[9];
  iv[5] = header[10];
  fillRandom(iv + 6, 6);

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, AES_KEY, 256);
  if (rc == 0) {
    rc = mbedtls_gcm_crypt_and_tag(
      &gcm,
      MBEDTLS_GCM_ENCRYPT,
      ptLen,
      iv, IV_LEN,
      header, HEADER_LEN,
      (const unsigned char*)msg.c_str(),
      ciphertext,
      TAG_LEN,
      tag
    );
  }

  mbedtls_gcm_free(&gcm);

  if (rc != 0) {
    free(packet);
    err = "Encryption failed";
    return false;
  }

  bool ok = base64EncodeBuf(packet, rawLen, outB64);
  free(packet);

  if (!ok) {
    err = "Base64 encode failed";
    return false;
  }

  return true;
}

bool replayWouldBeAccepted(uint32_t peerBootId, uint32_t peerCounter) {
  if (!haveReplayState) return true;
  if (peerBootId != lastPeerBootId) return true;
  return peerCounter > lastPeerCounter;
}

void updateReplayState(uint32_t peerBootId, uint32_t peerCounter) {
  haveReplayState = true;
  lastPeerBootId = peerBootId;
  lastPeerCounter = peerCounter;
}

bool decryptMessageFromBase64(const String& inB64,
                              uint16_t expectedFrom,
                              String& outText,
                              uint32_t& outPeerBootId,
                              uint32_t& outPeerCounter,
                              String& err) {
  uint8_t* raw = nullptr;
  size_t rawLen = 0;

  if (!base64DecodeBuf(inB64, raw, rawLen)) {
    err = "Base64 decode failed";
    return false;
  }

  if (rawLen < HEADER_LEN + IV_LEN + TAG_LEN) {
    free(raw);
    err = "Packet too short";
    return false;
  }

  const uint8_t* header = raw;
  const uint8_t* iv = raw + HEADER_LEN;
  const uint8_t* ciphertext = raw + HEADER_LEN + IV_LEN;
  size_t ctLen = rawLen - HEADER_LEN - IV_LEN - TAG_LEN;
  const uint8_t* tag = raw + rawLen - TAG_LEN;

  if (header[0] != SECURE_PACKET_VERSION) {
    free(raw);
    err = "Unsupported version";
    return false;
  }

  uint16_t sender = ((uint16_t)header[1] << 8) | header[2];
  outPeerBootId = getU32BE(header + 3);
  outPeerCounter = getU32BE(header + 7);

  if (sender != expectedFrom) {
    free(raw);
    err = "Sender mismatch";
    return false;
  }

  if (!replayWouldBeAccepted(outPeerBootId, outPeerCounter)) {
    free(raw);
    err = "Replay blocked";
    return false;
  }

  uint8_t* plaintext = (uint8_t*)malloc(ctLen + 1);
  if (!plaintext) {
    free(raw);
    err = "Out of memory";
    return false;
  }

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, AES_KEY, 256);
  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(
      &gcm,
      ctLen,
      iv, IV_LEN,
      header, HEADER_LEN,
      tag, TAG_LEN,
      ciphertext,
      plaintext
    );
  }

  mbedtls_gcm_free(&gcm);
  free(raw);

  if (rc != 0) {
    free(plaintext);
    err = "Authentication failed";
    return false;
  }

  plaintext[ctLen] = 0;
  outText = sanitizeMessage(String((char*)plaintext));
  free(plaintext);

  updateReplayState(outPeerBootId, outPeerCounter);
  return true;
}

// -----------------------------
// LoRa
// -----------------------------
void handleLoRaLine(const String& line);

bool sendATAndWait(const String& cmd, const String& expected, uint32_t timeoutMs = 2500) {
  Serial.println(">> " + cmd);
  LoRaSerial.print(cmd);
  LoRaSerial.print("\r\n");

  String line;
  uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    while (LoRaSerial.available()) {
      char c = (char)LoRaSerial.read();

      if (c == '\n') {
        line.trim();
        if (line.length() > 0) {
          Serial.println("<< " + line);

          if (line.startsWith("+RCV=")) {
            handleLoRaLine(line);
          } else if (expected.length() == 0 || line.indexOf(expected) >= 0) {
            return true;
          } else if (line == "ERROR" || line.startsWith("+ERR")) {
            return false;
          }
        }
        line = "";
      } else if (c != '\r') {
        line += c;
        if (line.length() > 512) {
          line.remove(0, line.length() - 512);
        }
      }
    }
    delay(1);
  }

  return false;
}

void parseIncomingRCV(const String& line) {
  int eq = line.indexOf('=');
  if (eq < 0) return;

  String rest = line.substring(eq + 1);

  int c1 = rest.indexOf(',');
  int c2 = rest.indexOf(',', c1 + 1);
  int cLast = rest.lastIndexOf(',');
  int cPrev = rest.lastIndexOf(',', cLast - 1);

  if (c1 < 0 || c2 < 0 || cPrev < 0 || cLast < 0 || cPrev <= c2) {
    addLog("SYS", 0, "Malformed +RCV line");
    return;
  }

  uint16_t from = (uint16_t)rest.substring(0, c1).toInt();
  int len = rest.substring(c1 + 1, c2).toInt();
  String payload = rest.substring(c2 + 1, cPrev);
  int rssi = rest.substring(cPrev + 1, cLast).toInt();
  int snr = rest.substring(cLast + 1).toInt();

  if (len >= 0 && (size_t)len < payload.length()) {
    payload = payload.substring(0, len);
  }

  String plaintext;
  uint32_t peerBoot = 0;
  uint32_t peerCounter = 0;
  String err;

  bool ok = decryptMessageFromBase64(payload, from, plaintext, peerBoot, peerCounter, err);
  if (ok) {
    addLog("RX", from, plaintext, rssi, snr);
    statusLine = "Last RX ok from " + String(from);
  } else {
    addLog("SYS", from, "Dropped packet: " + err, rssi, snr);
    statusLine = "RX blocked";
  }
}

void handleLoRaLine(const String& line) {
  if (line.startsWith("+RCV=")) {
    parseIncomingRCV(line);
  } else {
    Serial.println("[LoRa] " + line);
  }
}

void pumpLoRaSerial() {
  while (LoRaSerial.available()) {
    char c = (char)LoRaSerial.read();

    if (c == '\n') {
      serialLine.trim();
      if (serialLine.length()) {
        handleLoRaLine(serialLine);
      }
      serialLine = "";
    } else if (c != '\r') {
      serialLine += c;
      if (serialLine.length() > 512) {
        serialLine.remove(0, serialLine.length() - 512);
      }
    }
  }
}

bool initLoRa() {
  LoRaSerial.begin(LORA_BAUD, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);
  delay(800);

  bool ok = true;
  ok &= sendATAndWait("AT", "+OK");
  ok &= sendATAndWait("AT+MODE=0", "+OK");
  ok &= sendATAndWait("AT+ADDRESS=" + String(DEVICE_ADDR), "+OK");

  bool netOk = sendATAndWait("AT+NETWORKID=" + String(LORA_NETWORK_ID), "+OK");
  bool bandOk = sendATAndWait("AT+BAND=" + String(LORA_BAND), "+OK");

  if (!netOk) addLog("SYS", 0, "Could not set NETWORKID");
  if (!bandOk) addLog("SYS", 0, "Could not set BAND");

  loraReady = ok && netOk && bandOk;

  if (loraReady) {
    addLog("SYS", 0, "Consent-aware encrypted link ready");
    statusLine = "Encrypted link ready";
  } else {
    addLog("SYS", 0, "LoRa init failed");
    statusLine = "LoRa init failed";
  }

  return loraReady;
}

bool sendSecureLoRaMessage(const String& rawText) {
  if (!loraReady) return false;

  String msg = sanitizeMessage(rawText);
  if (msg.isEmpty()) return false;

  String encB64;
  String err;
  if (!encryptMessageToBase64(msg, encB64, err)) {
    addLog("SYS", 0, "Encrypt failed: " + err);
    statusLine = "Encrypt failed";
    return false;
  }

  if (encB64.length() > MAX_LORA_AT_DATA_LEN) {
    addLog("SYS", 0, "Encrypted payload too large");
    statusLine = "Payload too large";
    return false;
  }

  String cmd = "AT+SEND=" + String(REMOTE_ADDR) + "," + String(encB64.length()) + "," + encB64;
  bool ok = sendATAndWait(cmd, "+OK", 3500);

  if (ok) {
    addLog("TX", REMOTE_ADDR, msg);
    statusLine = "Last TX ok";
  } else {
    addLog("SYS", 0, "TX failed");
    statusLine = "Last TX failed";
  }

  return ok;
}

// -----------------------------
// Web
// -----------------------------
bool ensureAuthenticated() {
  if (server.authenticate(WEB_USER, WEB_PASS)) {
    return true;
  }
  server.requestAuthentication();
  return false;
}

void sendNoCacheHeaders() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.sendHeader("X-Content-Type-Options", "nosniff");
}

void handleRoot() {
  if (!ensureAuthenticated()) return;

  String page = FPSTR(INDEX_HTML);
  page.replace("%NODE%", DEVICE_NAME);
  page.replace("%SSID%", AP_SSID);
  page.replace("%IP%", WiFi.softAPIP().toString());
  page.replace("%ADDR%", String(DEVICE_ADDR));
  page.replace("%REMOTE%", String(REMOTE_ADDR));

  sendNoCacheHeaders();
  server.send(200, "text/html; charset=utf-8", page);
}

void handleMessages() {
  if (!ensureAuthenticated()) return;
  sendNoCacheHeaders();
  server.send(200, "application/json; charset=utf-8", buildMessagesJson());
}

void handleSend() {
  if (!ensureAuthenticated()) return;

  if (!server.hasArg("m")) {
    server.send(400, "text/plain", "Missing message");
    return;
  }

  String msg = sanitizeMessage(server.arg("m"));
  if (msg.isEmpty()) {
    server.send(400, "text/plain", "Empty message");
    return;
  }

  if (!loraReady) {
    server.send(503, "text/plain", "LoRa not ready");
    return;
  }

  bool ok = sendSecureLoRaMessage(msg);
  if (ok) {
    server.send(200, "text/plain", "OK");
  } else {
    server.send(500, "text/plain", "Send failed");
  }
}

void handleClear() {
  if (!ensureAuthenticated()) return;
  clearLogs();
  statusLine = "Log cleared";
  server.send(200, "text/plain", "OK");
}

void handleZine() {
  if (!ensureAuthenticated()) return;

  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Field Notes</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background: linear-gradient(135deg, #240046, #7b2cbf, #c9184a, #ff9e00);
      color: #fff7ed;
      margin: 0;
      padding: 20px;
    }
    .card {
      max-width: 760px;
      margin: auto;
      background: rgba(17, 24, 39, 0.92);
      padding: 22px;
      border-radius: 14px;
      border: 1px solid rgba(255,255,255,0.18);
      box-shadow: 0 14px 40px rgba(0,0,0,0.35);
    }
    h1 { margin-top: 0; color: #f9a8d4; }
    h2 { color: #fde68a; margin-top: 22px; }
    p, li { line-height: 1.6; }
    a { color: #93c5fd; }
    .small { color: #e9d5ff; font-size: 0.95rem; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Field Notes on Care</h1>
    <p class="small">A quiet page inside the project.</p>

    <p>This system was designed around privacy, consent, accessibility, and community care. The technology is small, but the design principle is larger: communication tools should reduce harm rather than create new kinds of exposure.</p>

    <h2>Values</h2>
    <ul>
      <li>Care is infrastructure.</li>
      <li>Access is part of justice.</li>
      <li>Consent belongs in design.</li>
      <li>Safety should be built in from the start.</li>
      <li>Community begins with listening.</li>
    </ul>

    <h2>Why this matters</h2>
    <p>For projects connected to gender, identity, equity, or vulnerability, even small design choices can affect whether people feel safe participating. A neutral public face and a values-rich private layer help balance visibility, usability, and care.</p>

    <p><a href="/">Back to chat</a></p>
  </div>
</body>
</html>
)rawliteral";

  sendNoCacheHeaders();
  server.send(200, "text/html; charset=utf-8", page);
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/messages", HTTP_GET, handleMessages);
  server.on("/send", HTTP_POST, handleSend);
  server.on("/clear", HTTP_POST, handleClear);
  server.on("/zine", HTTP_GET, handleZine);

  server.onNotFound([]() {
    if (!ensureAuthenticated()) return;
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
}

void startAccessPoint() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD, 6, 0, 1);
  IPAddress ip = WiFi.softAPIP();

  if (ok) {
    Serial.printf("AP started: %s  IP: %s\n", AP_SSID, ip.toString().c_str());
    addLog("SYS", 0, "Join Wi-Fi '" + String(AP_SSID) + "' and open http://" + ip.toString());
  } else {
    addLog("SYS", 0, "Wi-Fi AP start failed");
  }
}

// -----------------------------
// Arduino
// -----------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  bootId = esp_random();
  txCounter = 0;

  Serial.println();
  Serial.println("====================================");
  Serial.println("Secure LoRa Web Chat");
  Serial.println(DEVICE_NAME);
  Serial.println("====================================");

  addLog("SYS", 0, "Booting " + String(DEVICE_NAME));

  startAccessPoint();
  setupWebServer();
  initLoRa();
}

void loop() {
  server.handleClient();
  pumpLoRaSerial();
  delay(2);
}