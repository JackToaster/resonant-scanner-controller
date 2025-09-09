
#include <WiFi.h>
#include <NetworkClient.h>

#define CONFIG_ASYNC_TCP_RUNNING_CORE 1

#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>


const char *ssid = "Pretzel Bakery";
const char *password = "s0urd0ugh";

AsyncWebServer* server;

const int led = LED_BUILTIN;


const int coil_p = 9;
const int coil_n = 8;

const int coil_pwm_freq = 100000;

const int laser_pin = 7;
const int laser_pwm_freq = 1000000;


unsigned long coil_x_period_us = 1000000 / 154.5;
unsigned long coil_y_period_us = 1000000 / 1145.475;

int coil_x_amplitude = 0.03 * (1 << 16);
int coil_y_amplitude = 0.95 * (1 << 16);

int x_pix_phase = (43.0/360.0) * coil_x_period_us;
int y_pix_phase = (354.0/360.0) * coil_y_period_us;

const int min_coil_amplitude = 0.1 * 256;
const int max_coil_amplitude = 0.9 * 256;

const int coil_channel = 0; // LEDC channel for coil pwm
const int laser_channel = 1; // LEDC channel for laser pwm

volatile bool overflow = false;


uint32_t it_count = 0;


char send_buffer[16536];


uint8_t active_fb = 0;
const int FB_WIDTH=64;
const int FB_HEIGHT=64;

static SemaphoreHandle_t mutex; // mutex for video
File videoPlaying;
bool videoFileOpen = false;
bool newFileReady = false;
String newFileName;

uint8_t frame_buffer[2][FB_WIDTH][FB_HEIGHT];


void send_err(AsyncWebServerRequest * request) {
  String message = "500 Internal server error\n\n";
  message += "URI: ";
  message += request->url();
  message += "\nMethod: ";
  message += (request->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += request->args();
  message += "\n";

  for (uint8_t i = 0; i < request->args(); i++) {
    message += " " + request->argName(i) + ": " + request->arg(i) + "\n";
  }

  request->send(500, "text/plain", message);
}

// list all of the files, if ishtml=true, return html rather than simple text
String listFiles(bool ishtml) {
  String returnText = "";
  // Serial.println("Listing files stored on SPIFFS");
  File root = SPIFFS.open("/");
  File foundfile = root.openNextFile();
  
  while (foundfile) {
    returnText += "File: " + String(foundfile.name()) + "\n";
    
    foundfile = root.openNextFile();
  }
  root.close();
  foundfile.close();
  return returnText;
}

// Make size of files human readable
// source: https://github.com/CelliesProjects/minimalUploadAuthESP32
String humanReadableSize(const size_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  else if (bytes < (1024 * 1024)) return String(bytes / 1024.0) + " KB";
  else if (bytes < (1024 * 1024 * 1024)) return String(bytes / 1024.0 / 1024.0) + " MB";
  else return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
}

void handleRoot(AsyncWebServerRequest * request) {
  digitalWrite(led, 1);
  int sec = millis() / 1000;
  int hr = sec / 3600;
  int min = (sec / 60) % 60;
  sec = sec % 60;

  char* buf = send_buffer;
  int capacity = sizeof(send_buffer) - 1;
  int c = snprintf(
    buf, capacity,
    "<html>\
      <head>\
        <title>Resonant Scanner Driver</title>\
      </head>\
      <body>"
  );

  buf += c; capacity -= c;
  if(c<0 || capacity<0) send_err(request);


  if(overflow) {
    overflow = false;
    c = snprintf(
      buf, capacity, "<p style=\"color:red\">PWM Overflow occurred!</p>"
    );

    buf += c; capacity -= c;
    if(c<0 || capacity<0) send_err(request);
  }

  c = snprintf(
    buf, capacity,
       "<h1>Resonant Scanner</h1>\
        <p>Uptime: %02d:%02d:%02d</p>\
        <p>PWM Frequency: %d</p>\
        <h2>X Scan</h2>\
        <p>Amplitude: %.1f%%</p>\
        <p>Frequency: %.2fHz</p>\
        \
        <h2>Y Scan</h2>\
        <p>Amplitude: %.1f%%</p>\
        <p>Frequency: %.2fHz</p>",
    hr, min, sec,
    ledcReadFreq(coil_p),

    (float) coil_x_amplitude * 100.0 / (float)(1<<16),
    1000000.0 / (float) coil_x_period_us,

    (float) coil_y_amplitude * 100.0 / (float)(1<<16),
    1000000.0 / (float) coil_y_period_us
  );

  buf += c; capacity -= c;
  if(c<0 || capacity<0) send_err(request);

  c = snprintf(
    buf, capacity,
       "<h2>Phasing</h1>\
        <p>X: %.1fdeg</p>\
        <p>Y: %.1fdeg</p>",
    360.0 * (float)x_pix_phase / (float)coil_x_period_us,
    360.0 * (float)y_pix_phase / (float)coil_y_period_us
  );

  buf += c; capacity -= c;
  if(c<0 || capacity<0) send_err(request);


  c = snprintf(
    buf, capacity,
    " <hr><h2>Control</h2>\
      <iframe name=\"dummyframe\" id=\"dummyframe\" style=\"display: none;\"></iframe>\
      <form action=\"/control\" method=\"post\" target=\"dummyframe\">\
        <label for=\"Xf\">X Frequency (Hz):</label>\
        <input type=\"number\" id=\"Xf\" name=\"Xf\" value=\"%f\" step=\"any\"><br>\
        <label for=\"Xa\">X Amplitude (%%):</label>\
        <input type=\"number\" id=\"Xa\" name=\"Xa\" value=\"%f\" step=\"any\"><br>\
        <br>\
        \
        <label for=\"Yf\">Y Frequency (Hz):</label>\
        <input type=\"number\" id=\"Yf\" name=\"Yf\" value=\"%f\" step=\"any\"><br>\
        <label for=\"Ya\">Y Amplitude (%%):</label>\
        <input type=\"number\" id=\"Ya\" name=\"Ya\" value=\"%f\" step=\"any\"><br>\
        <br>\
        \
        <label for=\"Xpha\">X Phase (%%):</label>\
        <input type=\"number\" id=\"Xpha\" name=\"Xpha\" value=\"%f\" step=\"any\"><br>\
        <label for=\"Ypha\">Y Phase (%%):</label>\
        <input type=\"number\" id=\"Ypha\" name=\"Ypha\" value=\"%f\" step=\"any\"><br>\
        <br>\
        \
        <input type=\"submit\" value=\"Submit\">\
      </form>",
      1000000.0 / (float) coil_x_period_us, (float) coil_x_amplitude * 100.0 / (float)(1<<16),

      1000000.0 / (float) coil_y_period_us, (float) coil_y_amplitude * 100.0 / (float)(1<<16),

      360.0 * (float)x_pix_phase / (float)coil_x_period_us,
      360.0 * (float)y_pix_phase / (float)coil_y_period_us
  );
  
  buf += c; capacity -= c;
  if(c<0 || capacity<0) send_err(request);

  c = snprintf(
    buf, capacity,
    " <hr><h2>File Upload</h2>\
      <p>Free Storage: %s | Used Storage: %s | Total Storage: %s</p>\
      <form method=\"POST\" action=\"/upload\" enctype=\"multipart/form-data\" target=\"dummyframe\"><input type=\"file\" name=\"data\"/><input type=\"submit\" name=\"upload\" value=\"Upload\" title=\"Upload File\"></form>",
    humanReadableSize((SPIFFS.totalBytes() - SPIFFS.usedBytes())),
    humanReadableSize(SPIFFS.usedBytes()),
    humanReadableSize(SPIFFS.totalBytes())
  );
  
  buf += c; capacity -= c;
  if(c<0 || capacity<0) send_err(request);

  c = snprintf(
    buf, capacity,
    " <hr><h2>Files</h2>"
  );
  
  buf += c; capacity -= c;
  if(c<0 || capacity<0) send_err(request);

  File root = SPIFFS.open("/");
  File foundfile = root.openNextFile();
  
  while (foundfile) {
    c = snprintf(
      buf, capacity,
      "<p>%s <form method=\"POST\" action=\"/play\" target=\"dummyframe\"><input type=\"hidden\" name=\"file\" value=\"%s\"/><input type=\"submit\" name=\"play\" value=\"Play\" title=\"Play File\"></form></p>",
      foundfile.name(), foundfile.name()
    );
    
    buf += c; capacity -= c;
    if(c<0 || capacity<0) send_err(request);
    
    foundfile = root.openNextFile();
  }
  root.close();
  foundfile.close();
  
  c = snprintf(
    buf, capacity,
      "</body>\
    </html>"
  );
  
  buf += c; capacity -= c;
  if(c<0 || capacity<0) send_err(request);
  request->send(200, "text/html", send_buffer);
  digitalWrite(led, 0);
}

void handleControl(AsyncWebServerRequest *request) {
  // portENTER_CRITICAL(&timerMux);
  for (int i = 0; i < request->args(); i++) {
    String name = request->argName(i);

    float arg = request->arg(i).toFloat();

    if(name == "Xf") {
      coil_x_period_us = 1000000 / arg;
    } else if(name == "Yf") {
      coil_y_period_us = 1000000 / arg;
    } else if(name == "Xa") {
      coil_x_amplitude = arg / 100.0 * (float)(1 << 16);
    } else if(name == "Ya") {
      coil_y_amplitude = arg / 100.0 * (float)(1 << 16);
    } else if(name == "Xpha") {
        x_pix_phase = arg / 360.0 * (float)coil_x_period_us;
    } else if(name == "Ypha") {
        y_pix_phase = arg / 360.0 * (float)coil_y_period_us;
    }
  }
  // portEXIT_CRITICAL(&timerMux);
  
  // request->redirect("/");

  request->send(200, "text/html", "ok");
}


void readToFrameBuffer() {
  int next_fb = (active_fb + 1) % 2;
  uint8_t byte;
  bool light = false;
  uint8_t* flat_fb = (uint8_t*)(void*)&frame_buffer[next_fb];
  
  // un-RLE the data into the framebuffer
  for(int i = 0; i < FB_WIDTH * FB_HEIGHT; ) {
    if(videoPlaying.read(&byte, 1) == 0) {
      videoPlaying.close(); // out of file :/
      Serial.println("File done!");
      Serial.print(i);
      Serial.println(" extra bytes in frame");
      videoFileOpen = false;
      return;
    }
    for(int j = 0; j < byte; ++j) {
      if(i >= FB_WIDTH * FB_HEIGHT) {
        Serial.println("decoding overflows frame!");
        break;
      }
      flat_fb[i++] = light? 1:0;
    }
    light = !light;
  };
  active_fb = next_fb;
}

void handlePlay(AsyncWebServerRequest *request) {
  String filename = request->arg("file");
  // Serial.print("Plaing file: ");
  // Serial.println(filename);
  // request->redirect("/");
  // xSemaphoreTake(mutex, portMAX_DELAY);
  if(videoFileOpen) {
    videoFileOpen=false;
    videoPlaying.close();
  }
  videoPlaying = SPIFFS.open("/"+filename, "r");
  if (videoPlaying == NULL) {
      Serial.println("Can't open file!");
      send_err(request);
      return;
  }
  uint8_t byte;
  videoFileOpen=true;

  // xSemaphoreGive(mutex);
  // readToFrameBuffer(filename); // old for images only
  request->send(200, "text/html", "ok");
}


// handles uploads
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
  Serial.println(logmessage);

  if (!index) {
    logmessage = "Upload Start: " + String(filename);
    // open the file on first call and store the file handle in the request object
    request->_tempFile = SPIFFS.open("/" + filename, "w");
    Serial.println(logmessage);
  }

  if (len) {
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);
    logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
    Serial.println(logmessage);
  }

  if (final) {
    logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
    // close the file handle as the upload is now done
    request->_tempFile.close();
    Serial.println(logmessage);
    // request->redirect("/");
    request->send(200, "text/html", "ok");
  }
}

void handleNotFound(AsyncWebServerRequest * request) {
  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += request->url();
  message += "\nMethod: ";
  message += (request->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += request->args();
  message += "\n";

  for (int i = 0; i < request->args(); i++) {
    message += " " + request->argName(i) + ": " + request->arg(i) + "\n";
  }

  request->send(404, "text/plain", message);
  digitalWrite(led, 0);
}

const int sineTableSize = 256;
const int sineLookupTable[sineTableSize] = {
1, 406, 812, 1217, 1621, 2025, 2427, 2827, 3226, 3623, 4018, 4411, 4801, 5187, 5571, 5952,
6328, 6701, 7070, 7435, 7795, 8151, 8501, 8847, 9187, 9522, 9851, 10174, 10491, 10801, 11105, 11402,
11693, 11976, 12252, 12521, 12783, 13036, 13282, 13520, 13749, 13971, 14183, 14388, 14584, 14770, 14948, 15117,
15277, 15428, 15569, 15701, 15824, 15937, 16040, 16134, 16218, 16293, 16357, 16412, 16456, 16491, 16516, 16531,
16536, 16531, 16516, 16491, 16456, 16412, 16357, 16293, 16218, 16134, 16040, 15937, 15824, 15701, 15569, 15428,
15277, 15117, 14948, 14770, 14584, 14388, 14183, 13971, 13749, 13520, 13282, 13036, 12783, 12521, 12252, 11976,
11693, 11402, 11105, 10801, 10491, 10174, 9851, 9522, 9187, 8847, 8501, 8151, 7795, 7435, 7070, 6701,
6328, 5952, 5571, 5187, 4801, 4411, 4018, 3623, 3226, 2827, 2427, 2025, 1621, 1217, 812, 406,
1, -405, -811, -1216, -1620, -2024, -2426, -2826, -3225, -3622, -4017, -4410, -4800, -5186, -5570, -5951,
-6327, -6700, -7069, -7434, -7794, -8150, -8500, -8846, -9186, -9521, -9850, -10173, -10490, -10800, -11104, -11401,
-11692, -11975, -12251, -12520, -12782, -13035, -13281, -13519, -13748, -13970, -14182, -14387, -14583, -14769, -14947, -15116,
-15276, -15427, -15568, -15700, -15823, -15936, -16039, -16133, -16217, -16292, -16356, -16411, -16455, -16490, -16515, -16530,
-16535, -16530, -16515, -16490, -16455, -16411, -16356, -16292, -16217, -16133, -16039, -15936, -15823, -15700, -15568, -15427,
-15276, -15116, -14947, -14769, -14583, -14387, -14182, -13970, -13748, -13519, -13281, -13035, -12782, -12520, -12251, -11975,
-11692, -11401, -11104, -10800, -10490, -10173, -9850, -9521, -9186, -8846, -8500, -8150, -7794, -7434, -7069, -6700,
-6327, -5951, -5570, -5186, -4800, -4410, -4017, -3622, -3225, -2826, -2426, -2024, -1620, -1216, -811, -405};

// calculate duty cycle for coil driver
int calc_duty(uint8_t x, uint8_t y) {
  int coil_x = (sineLookupTable[x] * coil_x_amplitude) >> 15; // TODO Interpolate (maybe not needed for coil drive)
  int coil_y = (sineLookupTable[y] * coil_y_amplitude) >> 15;

  int coil = coil_x + coil_y;

  if(coil > 32768 || coil < -32768) {
    overflow = true;
  }

  int duty = (((max_coil_amplitude - min_coil_amplitude) * coil) >> 16) + (min_coil_amplitude + max_coil_amplitude) / 2;

  return duty;
}

bool pixel = false;
uint8_t calc_pixel(unsigned int x_time, unsigned int y_time) {
  unsigned int x_time_adj = (x_time + x_pix_phase) % coil_x_period_us;
  unsigned int y_time_adj = (y_time + y_pix_phase) % coil_y_period_us;

  uint8_t pix_x_pos = (x_time_adj) * sineTableSize / coil_x_period_us;
  uint8_t pix_y_pos = (y_time_adj) * sineTableSize / coil_y_period_us;


  int pix_x = sineLookupTable[pix_x_pos]; // todo interpolate
  int pix_y = sineLookupTable[pix_y_pos];
  
  int pix_x_co = (pix_x + (1<<14)) * FB_WIDTH / (1<<15);
  int pix_y_co = (pix_y + (1<<14)) * FB_HEIGHT / (1<<15);

  if(pix_x_co >= FB_WIDTH) pix_x_co = FB_WIDTH - 1;
  if(pix_y_co >= FB_HEIGHT) pix_y_co = FB_HEIGHT - 1;

  return frame_buffer[active_fb][pix_x_co][pix_y_co];
  // pixel = !pixel;
  // return pixel?7:0;
}

void updateDisplay() {
  // digitalWrite(led, 1);
  unsigned long time = micros();
  ++it_count;


  // portENTER_CRITICAL_ISR(&timerMux);
  unsigned int coil_x_time = time % coil_x_period_us;
  unsigned int coil_y_time = time % coil_y_period_us;
  uint8_t coil_x_pos = (coil_x_time) * sineTableSize / coil_x_period_us;
  uint8_t coil_y_pos = (coil_y_time) * sineTableSize / coil_y_period_us;
  // portEXIT_CRITICAL_ISR(&timerMux);

  int duty = calc_duty(coil_x_pos, coil_y_pos);
  
  ledcWriteChannel(coil_channel, duty);

  uint8_t pixel = calc_pixel(coil_x_time, coil_y_time);

  ledcWriteChannel(laser_channel, pixel);

  // digitalWrite(led, 0);
}

void displayFrame() {
  while((micros() % coil_x_period_us) < coil_x_period_us / 2) {
    updateDisplay();
  }
  while((micros() % coil_x_period_us) > coil_x_period_us / 2) {
    updateDisplay();
  }
}

TaskHandle_t VideoTask;
void playVideo(void * parameter){
  Serial.println("Video playback started");
  while(1){
    delay(1000 / 30);
    // if(xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE){
      if(videoFileOpen) {
        readToFrameBuffer(); // read and decode next frame
        // videoFileOpen=false;
        // Serial.println("frame");
      }
      // xSemaphoreGive(mutex);
    // }
  }
}


void setup(void) {
  mutex = xSemaphoreCreateMutex();

  setCpuFrequencyMhz(240);
  Serial.begin(115200);

  Serial.println("Mounting SPIFFS ...");
  if (!SPIFFS.begin(true)) {
    // if you have not used SPIFFS before on a ESP32, it will show this error.
    // after a reboot SPIFFS will be configured and will happily work.
    Serial.println("ERROR: Cannot mount SPIFFS");
  }

  Serial.print("SPIFFS Free: "); Serial.println(humanReadableSize((SPIFFS.totalBytes() - SPIFFS.usedBytes())));
  Serial.print("SPIFFS Used: "); Serial.println(humanReadableSize(SPIFFS.usedBytes()));
  Serial.print("SPIFFS Total: "); Serial.println(humanReadableSize(SPIFFS.totalBytes()));
  Serial.println(listFiles(false));



  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started");
  }
  server = new AsyncWebServer(80);
  server->on("/", HTTP_GET, handleRoot);
  server->on("/control", handleControl);
  server->on("/play", handlePlay);

  // run handleUpload function when any file is uploaded
  server->on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
      request->send(200);
    }, handleUpload);

  server->onNotFound(handleNotFound);
  server->begin();
  Serial.println("HTTP server started");

  // PWM setup
  bool success = true;
  success &= ledcAttachChannel(coil_p, coil_pwm_freq, LEDC_TIMER_8_BIT, coil_channel);
  success &= ledcAttachChannel(coil_n, coil_pwm_freq, LEDC_TIMER_8_BIT, coil_channel);
  success &= ledcOutputInvert(coil_p, false);
  success &= ledcOutputInvert(coil_n, true);
  ledcWriteChannel(coil_channel, 128); // 50% duty

  success &= ledcAttachChannel(laser_pin, laser_pwm_freq, LEDC_TIMER_1_BIT, laser_channel);
  ledcWriteChannel(laser_channel, 0); // off% duty

  Serial.print("PWM setup success: ");
  Serial.println(success);

  // corner checker pattern for calibration
  for(int x = 0; x < FB_WIDTH; ++x) {
    for(int y = 0; y < FB_HEIGHT; ++y) {
      frame_buffer[0][x][y] = ((x&32) != (y&32)) ? 1:0;
    }
  }

  xTaskCreatePinnedToCore(
    playVideo,       // Function that should be called
    "Video",         // Name of the task (for debugging)
    32768,            // Stack size (bytes)
    NULL,            // Parameter to pass
    1,               // Task priority
    &VideoTask,      // Task handle
    0                // pin to core 0 to not interrupt scanning
  );
}

void loop(void) {
  displayFrame();
}