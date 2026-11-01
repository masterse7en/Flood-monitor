// Distance triggered camera capture for ESP32-S3 N16R8 CAM
//
// Sensor wiring (JSN-SR04T):
//   VCC  -> 3V3 (or 5V with a 1k + 2k divider on ECHO)
//   GND  -> GND
//   TRIG -> GPIO 1
//   ECHO -> GPIO 2
//
// GPIO 4 and 5 are used by the camera, so the sensor moved to GPIO 1 and 2.
// Photos save to internal flash (LittleFS). Open http://<board-ip>/ to view them.
// Serial Monitor: 115200 baud. The IP prints after WiFi connects.
//
// Arduino IDE Tools settings:
//   Board: ESP32S3 Dev Module
//   PSRAM: OPI PSRAM
//   Flash Size: 16MB
//   Partition Scheme: 16M Flash (2MB APP/12.5MB FATFS) or any 16MB scheme
//     with a SPIFFS or FATFS data partition
//   USB CDC On Boot: Disabled (when using the UART port)

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ---------- CONFIG ----------
// WiFi, URLs and the device key live in secrets.h (not committed).
// Copy secrets.example.h to secrets.h and fill in your values.
#include "secrets.h"

const char* DEVICE_ID = "flood-monitor-01";
#define POST_INTERVAL_MS 9000   // keep below MEASURE_INTERVAL_MS so every measurement posts
unsigned long lastPost = 0;

// Smaller distance means water is closer to the sensor, i.e. higher water.
// Adjust these to your real mount height and flood thresholds.
#define WARNING_DISTANCE_CM 100.0
#define DANGER_DISTANCE_CM  60.0
#define STATE_HYSTERESIS_CM 5.0
char currentState[8] = "NORMAL";

#define TRIG_PIN 1
#define ECHO_PIN 2

// Capture distances in cm. Edit freely.
const float TARGETS_CM[] = {50, 100, 150, 200, 250};
const int NUM_TARGETS = sizeof(TARGETS_CM) / sizeof(TARGETS_CM[0]);

#define TOLERANCE_CM 3.0     // capture when within this distance of a target
#define REARM_CM 10.0        // target re-arms after distance moves this far away
#define MIN_GAP_MS 2000      // minimum time between photos

#define MEASURE_INTERVAL_MS 10000   // one measurement every 10 seconds
#define BURST_COUNT 5               // quick pings per measurement, the median is used
#define BURST_GAP_MS 60             // pause between pings in a burst
#define MIN_VALID_PINGS 3           // need at least this many good pings
#define MIN_CM 25.0
#define MAX_CM 450.0

// Camera pin map (Freenove / ESP32-S3-EYE style boards)
#define XCLK_GPIO  15
#define SIOD_GPIO  4
#define SIOC_GPIO  5
#define Y9_GPIO    16
#define Y8_GPIO    17
#define Y7_GPIO    18
#define Y6_GPIO    12
#define Y5_GPIO    10
#define Y4_GPIO    8
#define Y3_GPIO    9
#define Y2_GPIO    11
#define VSYNC_GPIO 6
#define HREF_GPIO  7
#define PCLK_GPIO  13
// ----------------------------

WebServer server(80);

unsigned long lastMeasure = 0;
bool firstMeasure = true;
unsigned long lastCapture = 0;
bool armed[NUM_TARGETS];
float lastMedian = 0;
float prevMedian = -1;
bool cameraReady = false;
bool fsReady = false;

// ---------- SENSOR ----------
float readDistanceOnce() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(4);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 40000);
  if (duration == 0) return -1.0;
  return duration * 0.0343 / 2.0;
}

// Takes BURST_COUNT quick pings and returns their median.
// Returns -1 if fewer than MIN_VALID_PINGS were valid.
float measureBurst() {
  float s[BURST_COUNT];
  int n = 0;
  for (int i = 0; i < BURST_COUNT; i++) {
    float d = readDistanceOnce();
    if (d >= MIN_CM && d <= MAX_CM) s[n++] = d;
    delay(BURST_GAP_MS);
  }
  if (n < MIN_VALID_PINGS) return -1.0;

  for (int i = 1; i < n; i++) {
    float key = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > key) {
      s[j + 1] = s[j];
      j--;
    }
    s[j + 1] = key;
  }
  if (n % 2 == 1) return s[n / 2];
  return (s[n / 2 - 1] + s[n / 2]) / 2.0;
}

// ---------- STATE ----------
void updateState(float distanceCm) {
  if (strcmp(currentState, "NORMAL") == 0) {
    if (distanceCm <= DANGER_DISTANCE_CM) strcpy(currentState, "DANGER");
    else if (distanceCm <= WARNING_DISTANCE_CM) strcpy(currentState, "WARNING");
  } else if (strcmp(currentState, "WARNING") == 0) {
    if (distanceCm <= DANGER_DISTANCE_CM) strcpy(currentState, "DANGER");
    else if (distanceCm > WARNING_DISTANCE_CM + STATE_HYSTERESIS_CM) strcpy(currentState, "NORMAL");
  } else {
    if (distanceCm > DANGER_DISTANCE_CM + STATE_HYSTERESIS_CM) {
      strcpy(currentState, (distanceCm <= WARNING_DISTANCE_CM) ? "WARNING" : "NORMAL");
    }
  }
}

// ---------- CAMERA ----------
bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO;
  c.pin_d1 = Y3_GPIO;
  c.pin_d2 = Y4_GPIO;
  c.pin_d3 = Y5_GPIO;
  c.pin_d4 = Y6_GPIO;
  c.pin_d5 = Y7_GPIO;
  c.pin_d6 = Y8_GPIO;
  c.pin_d7 = Y9_GPIO;
  c.pin_xclk = XCLK_GPIO;
  c.pin_pclk = PCLK_GPIO;
  c.pin_vsync = VSYNC_GPIO;
  c.pin_href = HREF_GPIO;
  c.pin_sccb_sda = SIOD_GPIO;
  c.pin_sccb_scl = SIOC_GPIO;
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_VGA;
  c.jpeg_quality = 12;
  c.fb_count = 1;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  return true;
}

// ---------- S3 UPLOAD ----------
void uploadPhotoToS3(camera_fb_t* fb, int targetCm, float actualCm) {
  if (WiFi.status() != WL_CONNECTED || !fb) return;
  if (strstr(PHOTO_UPLOAD_URL, "YOUR-") != NULL) {
    Serial.println("S3 upload skipped. PHOTO_UPLOAD_URL not set.");
    return;
  }

  String url = String(PHOTO_UPLOAD_URL) +
               "?device_id=" + String(DEVICE_ID) +
               "&target=" + String(targetCm) +
               "&distance=" + String((int)actualCm);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setTimeout(15000);   // photos take longer than a JSON post
  if (!https.begin(client, url)) {
    Serial.println("S3 upload: begin failed");
    return;
  }
  https.addHeader("Content-Type", "image/jpeg");
  https.addHeader("x-device-key", DEVICE_KEY);

  int code = https.POST(fb->buf, fb->len);
  Serial.printf("S3 upload status: %d\n", code);
  if (code > 0) Serial.println(https.getString());
  https.end();
}

bool savePhoto(int targetCm, float actualCm) {
  if (!cameraReady) {
    Serial.println("Cannot capture. Camera not ready.");
    return false;
  }

  // Discard one stale frame so the saved photo is current
  camera_fb_t* old = esp_camera_fb_get();
  if (old) esp_camera_fb_return(old);

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Capture failed");
    return false;
  }

  // Upload first, so a full local storage never blocks the cloud copy
  uploadPhotoToS3(fb, targetCm, actualCm);

  if (!fsReady) {
    Serial.println("Local storage not ready. Photo sent to S3 only.");
    esp_camera_fb_return(fb);
    return true;
  }

  size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (freeBytes < fb->len + 20000) {
    Serial.println("Storage full. Open the web page and delete photos.");
    esp_camera_fb_return(fb);
    return false;
  }

  char path[48];
  snprintf(path, sizeof(path), "/t%03d_%dcm_%lus.jpg",
           targetCm, (int)actualCm, millis() / 1000);

  File f = LittleFS.open(path, FILE_WRITE);
  if (!f) {
    Serial.println("Could not open file");
    esp_camera_fb_return(fb);
    return false;
  }
  f.write(fb->buf, fb->len);
  f.close();
  Serial.printf("Saved %s (%u bytes)\n", path, (unsigned)fb->len);
  esp_camera_fb_return(fb);
  return true;
}

// ---------- AWS ----------
void postToAWS(float distanceCm, const char* state) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastPost < POST_INTERVAL_MS) return;
  lastPost = millis();

  WiFiClientSecure client;
  client.setInsecure();   // skip cert validation, fine for a Lambda Function URL

  HTTPClient https;
  if (!https.begin(client, LAMBDA_URL)) {
    Serial.println("AWS POST: begin failed");
    return;
  }
  https.addHeader("Content-Type", "application/json");
  https.addHeader("x-device-key", DEVICE_KEY);

  String payload = "{\"device_id\":\"" + String(DEVICE_ID) +
                    "\",\"distance_cm\":" + String(distanceCm, 1) +
                    ",\"state\":\"" + String(state) +
                    "\",\"timestamp\":" + String((unsigned long)(millis() / 1000)) + "}";

  int code = https.POST(payload);
  Serial.print("AWS POST status: ");
  Serial.println(code);
  https.end();
}

// ---------- WEB ----------
void handleRoot() {
  String h = "<html><body><h3>Captures</h3>";
  h += "<p id='usage'></p>";
  h += "<p><a href='/snap'>Take test photo</a> | <a href='/clear'>Delete all</a></p>";
  h += "<ul id='list'></ul>";
  h += "<script>";
  h += "async function refresh(){";
  h += "  const r = await fetch('/list');";
  h += "  const d = await r.json();";
  h += "  document.getElementById('usage').textContent = ";
  h += "    'Used ' + d.usedKB + ' KB of ' + d.totalKB + ' KB';";
  h += "  const ul = document.getElementById('list');";
  h += "  ul.innerHTML = '';";
  h += "  d.files.forEach(f => {";
  h += "    const li = document.createElement('li');";
  h += "    li.innerHTML = '<a href=\"/f?n=' + f.name + '\">' + f.name + '</a> (' + f.kb + ' KB)';";
  h += "    ul.appendChild(li);";
  h += "  });";
  h += "}";
  h += "refresh();";
  h += "setInterval(refresh, 3000);";
  h += "</script>";
  h += "</body></html>";
  server.send(200, "text/html", h);
}

void handleList() {
  String j = "{\"usedKB\":" + String(LittleFS.usedBytes() / 1024) +
             ",\"totalKB\":" + String(LittleFS.totalBytes() / 1024) +
             ",\"files\":[";
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  bool first = true;
  while (f) {
    String n = String(f.name());
    if (!n.startsWith("/")) n = "/" + n;
    if (!first) j += ",";
    j += "{\"name\":\"" + n + "\",\"kb\":" + String(f.size() / 1024) + "}";
    first = false;
    f = root.openNextFile();
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void handleFile() {
  String n = server.arg("n");
  if (!n.startsWith("/") || n.indexOf("..") >= 0 || !LittleFS.exists(n)) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  File f = LittleFS.open(n, "r");
  server.streamFile(f, "image/jpeg");
  f.close();
}

void handleSnap() {
  savePhoto(0, lastMedian);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleClear() {
  LittleFS.format();
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

// ---------- MAIN ----------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // Works with SPIFFS style schemes and FATFS style schemes
  fsReady = LittleFS.begin(true, "/littlefs", 10, "spiffs");
  if (!fsReady) fsReady = LittleFS.begin(true, "/littlefs", 10, "ffat");
  if (!fsReady) {
    Serial.println("Storage mount failed. Pick a 16MB partition scheme with SPIFFS or FATFS.");
  }

  cameraReady = initCamera();
  if (!cameraReady) {
    Serial.println("Check the camera pin map against your board's pinout.");
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Open http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi failed. Photos still save, but the web page is offline.");
  }

  server.on("/", handleRoot);
  server.on("/list", handleList);
  server.on("/f", handleFile);
  server.on("/snap", handleSnap);
  server.on("/clear", handleClear);
  server.begin();

  for (int i = 0; i < NUM_TARGETS; i++) armed[i] = true;
  Serial.println("Distance camera started");
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (!firstMeasure && now - lastMeasure < MEASURE_INTERVAL_MS) return;
  firstMeasure = false;
  lastMeasure = now;

  float med = measureBurst();
  if (med < 0) {
    Serial.println("No valid echo. Check wiring, target distance, or power.");
    return;
  }
  lastMedian = med;
  updateState(med);
  Serial.printf("distance_cm:%.1f state:%s\n", med, currentState);
  postToAWS(med, currentState);

  for (int i = 0; i < NUM_TARGETS; i++) {
    float t = TARGETS_CM[i];
    float diff = fabs(med - t);
    // Crossed: previous and current readings are on opposite sides of the target
    bool crossed = (prevMedian >= 0) && ((prevMedian - t) * (med - t) < 0);

    if (armed[i] && (diff <= TOLERANCE_CM || crossed) &&
        (millis() - lastCapture) > MIN_GAP_MS) {
      Serial.printf("Target %d cm reached at %.1f cm\n", (int)t, med);
      savePhoto((int)t, med);
      armed[i] = false;
      lastCapture = millis();
    } else if (!armed[i] && diff > REARM_CM) {
      armed[i] = true;
    }
  }
  prevMedian = med;
}
