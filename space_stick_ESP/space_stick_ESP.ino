
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <FastLED.h>
#include <EEPROM.h>

#include "colorutils.h"
#include "colorpalettes.h"
#include "GyverButton.h"

#ifndef APSSID
#define APSSID "space_stick"
#define APPSK  "spaceStick2020"
#endif

#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001000)
#warning "Requires FastLED 3.1 or later; check github for latest code."
#endif

#define NUM_LEDS    36       // adjust this to the number of LEDs you have: 16 or more
#define NUM_MODES 16        //Update this number to the highest number of "cases"
#define LED_TYPE    WS2812B // adjust this to the type of LEDS. This is for Neopixels
#define DATA_PIN    2       // adjust this to the pin you've connected your LEDs to   
// #define BRIGHTNESS  100  // 255 is full brightness, 128 is half, 32 is an eighth.
#define BTN1 0              // кнопка подключена сюда (PIN --- КНОПКА --- GND)
#define COLOR_ORDER GBR     // Try mixing up the letters (RGB, GBR, BRG, etc) for a whole new world of color combinations
#define MAX_BRIGHTNESS  255

GButton butt1(BTN1);

byte BRIGHTNESS = 100;

CRGB leds[NUM_LEDS];
CRGBPalette16 currentPalette;
TBlendType    currentBlending;

uint8_t gHue = 0;           // rotating "base color" used by many of the patterns
uint16_t SPEED = 10;        // SPEED set dynamically once we've started up
uint8_t ledMode = 15;

//space_stick parameters
byte prevKeyState = HIGH;   // button is active low

//rainbow parameters
uint8_t thisdelay = 40;     // A delay value for the sequence(s)
uint8_t thishue = gHue;     // Starting hue value.
int8_t thisrot = 1;         // Hue rotation speed. Includes direction.
uint8_t deltahue = 1;       // Hue change between pixels.
bool thisdir = 0;

//blendwave parameters
CRGB clr1;
CRGB clr2;
uint8_t speed;
uint8_t loc1;
uint8_t loc2;
uint8_t ran1;
uint8_t ran2;

//three_sin_demo parameters
int wave1 = 0;              // Current phase is calculated.
int wave2 = 0;
int wave3 = 0;

uint8_t inc1 = 2;           // Phase shift. Keep it low, as this is the speed at which the waves move.
uint8_t inc2 = 1;
uint8_t inc3 = -2;

uint8_t lvl1 = 80;          // Any result below this value is displayed as 0.
uint8_t lvl2 = 80;
uint8_t lvl3 = 80;

uint8_t mul1 = 20;          // Frequency, thus the distance between waves
uint8_t mul2 = 25;
uint8_t mul3 = 22;


//...parameters
const char *ssid = APSSID;
const char *password = APPSK;

ESP8266WebServer server(80);

void setup() {
  delay(1000);
  Serial.begin(115200);

  EEPROM.begin(512);
  byte flag_mode_wifi = EEPROM.read(0);
  Serial.print("FLAG_WIFI_");
  Serial.println(flag_mode_wifi);


  //Если флаг равен 1 то режим клиента wifi


  if (flag_mode_wifi == 1) {
    Serial.println("FLAG IS 1");
    //read ssid and passwd
    byte ssid_size = EEPROM.read(1);
    byte password_size = EEPROM.read(2);
    char ssid_wifi[ssid_size];
    char password_wifi[password_size];

    Serial.print("SSID_IS_");
    //Считываем ssid из памяти
    for (int i = 3; i < ssid_size + 3; ++i ) {
      ssid_wifi[i - 3] = (char) EEPROM.read(i);// -3 нужно для сдвига i для массива
      Serial.print((char) EEPROM.read(i));
    }
    Serial.println();
    Serial.print("PASSWD_IS_");
    //Считываем пароль из памяти
    for (int i = 3 + ssid_size; i < ssid_size + 3 + password_size; ++i ) {
      password_wifi[i - 3 - ssid_size] = (char) EEPROM.read(i);
      Serial.print((char) EEPROM.read(i));
    }
    Serial.println();

    //Здесь и далее выполняю преобразование массива char в String
    //Почему бы сразу не сделать это в верхних форах
    String wifi;
    for (int i = 0; i < sizeof(ssid_wifi); i++) {
      wifi = wifi + ssid_wifi[i];
    }

    String passwd;
    for (int i = 0; i < sizeof(password_wifi); i++) {
      passwd = passwd + password_wifi[i];
    }



    //connect
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi, passwd);//вот эти переменные нужно тащить из памяти
    Serial.println(wifi);
    Serial.println(passwd);

    EEPROM.write(0, 0);//Меняем флаг  для смены режима в точку доступа после перезагрузки
    EEPROM.commit();

    while (WiFi.status() != WL_CONNECTED) { // ЗДЕСЬ ПРОГРАММА ВСТАНЕТ
      delay(500);
      Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());



  }
  else {
    Serial.print("Configuring access point...");
    WiFi.softAP(ssid, password);

    IPAddress myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);

  }





  //API methods
  server.on("/api/check", HTTP_GET, handleCheckConnection);
  server.on("/api/mode", HTTP_POST, handlePattern);
  server.on("/api/mode/speed", HTTP_POST, handleSpeed);
  server.on("/api/mode/brightness", HTTP_POST, handleBrightness);
  server.on("/api/mode/pattern/color", HTTP_POST, handleColor);

  server.on("/api/switch/mode", HTTP_POST, handleSwitch);


  server.begin();

  Serial.println("HTTP server started");

  //Space_stick
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  currentBlending;
}

void loop() {

  server.handleClient();
  //
  //  butt1.tick();  // обязательная функция отработки. Должна постоянно опрашиваться
  //  if (butt1.isClick()) {           // одиночное нажатие  и lenMode не больше количества режимов
  //    ledMode=(ledMode+1) % NUM_MODES;              // инкремент
  //   // Serial.print(ledMode);
  //  }
  //
  //  if ( BRIGHTNESS >= MAX_BRIGHTNESS){
  //    BRIGHTNESS=10;
  //  }
  //
  //  if (butt1.isStep()) {            // обработчик удержания с шагами
  //    SPEED+=10;                       // увеличивать/уменьшать переменную value с шагом и интервалом!
  //   // Serial.print(SPEED);
  //  }

  switch (ledMode) {
    case 0: rainbow(); break;

    case 1: juggle(); break;

    case 2: sinelon(); break;

    case 3: confetti(); break;

    case 4: rainbowWithGlitter(); break;

    case 5: thisrot = 1; deltahue = 5; rainbow(); break;

    case 6: thisdir = -1; deltahue = 10; rainbow(); break;

    case 7: thisrot = 5; rainbow(); break;

    case 8: thisrot = 5; thisdir = -1; deltahue = 20; rainbow(); break;

    case 9: deltahue = 30; rainbow(); break;

    case 10: deltahue = 2; thisrot = 5; rainbow(); break;

    case 11: three_sin(); break;

    case 12: thisdelay = 25; mul1 = 20; mul2 = 25; mul3 = 22; lvl1 = 80; lvl2 = 80; lvl3 = 80; inc1 = 1; inc2 = 1; three_sin(); break;

    case 13: mul1 = 5; mul2 = 8; mul3 = 7; three_sin(); break;

    case 14: thisdelay = 40; lvl1 = 180; lvl2 = 180; lvl3 = 180; three_sin(); break;

    case 15: blendwave(); break;

    case 16: constColor(); break;

    default: ledMode = 0;
  }

  FastLED.show();
  FastLED.delay(1000 / SPEED);

  EVERY_N_MILLISECONDS( 20 ) {
    gHue++;  // slowly cycle the "base color" through the rainbow
    FastLED.setBrightness(BRIGHTNESS);
  }
}

//API handlers
void handleCheckConnection() {
  server.send(200);
}

void handlePattern() {
  if (server.hasArg("pattern")) {
    ledMode = (atoi(server.arg("pattern").c_str()));
    server.send(200);
    Serial.println(ledMode);
  }
}

void handleSpeed() {
  if (server.hasArg("s")) {
    SPEED = (atoi(server.arg("s").c_str()));
    server.send(200);
    Serial.println(SPEED);
  }
}

void handleBrightness() {
  if (server.hasArg("b")) {
    BRIGHTNESS = (atoi(server.arg("b").c_str()));
    server.send(200);
    Serial.println(BRIGHTNESS);
  }
}

void handleColor() {
  if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    fill_solid(leds, NUM_LEDS, CRGB(
                 atoi(server.arg("b").c_str()),
                 atoi(server.arg("g").c_str()),
                 atoi(server.arg("r").c_str())));

    server.send(200);
  }
}

void handleSwitch() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    byte ssid_size = strlen(server.arg("ssid").c_str());
    byte password_size = strlen(server.arg("password").c_str());

    const char* ssid_wifi = new char[ssid_size];
    const char* password_wifi = new char[password_size];

    ssid_wifi = server.arg("ssid").c_str();
    password_wifi = server.arg("password").c_str();


    //Поднимаем флаг и пишем размеры ssid и пароля в первые 3 ячейки
    Serial.println("Поднимаем флаг");
    EEPROM.write(0, 1);
    EEPROM.write(1, ssid_size);
    EEPROM.write(2, password_size);
    //Записываем ssid в  eeprom
    for (int i = 3; i < ssid_size + 3 ; i++) {
      EEPROM.write(i, ssid_wifi[i - 3]);
      Serial.print("ЗАпиСали ____");
      Serial.print(ssid_wifi[i - 3]);
    }

    Serial.println();// Разделитель вывода

    //Записываем пароль в  eeprom
    for (int i = ssid_size + 3; i < ssid_size + password_size + 3; i++) {
      EEPROM.write(i, password_wifi[i - 3 - ssid_size]);
      Serial.print("ЗАпиСали ____");
      Serial.print(password_wifi[i - 3 - ssid_size]);
    }
    EEPROM.commit();
    server.send(200);
    delay(500);
    ESP.restart();

  }
}

void constColor() {
}

//Space stick patterns
//GLITTER
void addGlitter( fract8 chanceOfGlitter) {
  if (random8() < chanceOfGlitter) {
    leds[random16(NUM_LEDS)] += CRGB::Gray;
  }
}

// I use a direction variable instead of signed math so I can use it in multiple routines.
void three_sin() {
  wave1 += inc1;
  wave2 += inc2;
  wave3 += inc3;
  for (int k = 0; k < NUM_LEDS; k++) {
    leds[k].r = qsub8(sin8(mul1 * k + wave1 / 128), lvl1);    // Another fixed frequency, variable phase sine wave with lowered level
    leds[k].g = qsub8(sin8(mul2 * k + wave2 / 128), lvl2);    // A fixed frequency, variable phase sine wave with lowered level
    leds[k].b = qsub8(sin8(mul3 * k + wave3 / 128), lvl3);    // A fixed frequency, variable phase sine wave with lowered level
  }
}

void rainbow() {
  // FastLED's built-in rainbow generator
  if (thisdir == 0) thishue += thisrot; else thishue -= thisrot; // I could use signed math, but 'thisdir' works with other routines.
  fill_rainbow(leds, NUM_LEDS, thishue, deltahue);
  // fadeToBlackBy( leds, NUM_LEDS, 255-BRIGHTNESS);
}

void blendwave() {

  speed = beatsin8(6, 0, 200);

  clr1 = blend(CHSV(beatsin8(3, 0, 200), 200, 200), CHSV(beatsin8(4, 0, 200), 200, 200), speed);
  clr2 = blend(CHSV(beatsin8(4, 0, 200), 200, 200), CHSV(beatsin8(3, 0, 200), 200, 200), speed);

  loc1 = beatsin8(10, 0, NUM_LEDS - 1);

  fill_gradient_RGB(leds, 0, clr2, loc1, clr1);
  fill_gradient_RGB(leds, loc1, clr2, NUM_LEDS - 1, clr1);

}

void rainbowWithGlitter() {
  // built-in FastLED rainbow, plus some random sparkly glitter
  rainbow();
  addGlitter(80);
}

void confetti() {
  // random colored speckles that blink in and fade smoothly
  fadeToBlackBy(leds, NUM_LEDS, 10);
  int pos = random16(NUM_LEDS);
  leds[pos] += CHSV(gHue + random8(64), 255, BRIGHTNESS * 2); //Adjusted Brightness with variable
}

void juggle() {
  // eight colored dots, weaving in and out of sync with each other
  fadeToBlackBy(leds, NUM_LEDS, 16);
  byte dothue = 0;
  for (int i = 0; i < 8; i++) {
    leds[beatsin16(i + 7, 0, NUM_LEDS)] |= CHSV(dothue, 255, BRIGHTNESS * 2); //Adjusted Brightness with variable
    dothue += 32;
  }
}

void sinelon() {
  // a colored dot sweeping back and forth, with fading trails
  fadeToBlackBy(leds, NUM_LEDS, 16);
  int pos = beatsin16(13, 0, NUM_LEDS);
  leds[pos] += CHSV(gHue, 255, BRIGHTNESS * 2);
}

void bpm() {
  // colored stripes pulsing at a defined Beats-Per-Minute (BPM)
  uint8_t BeatsPerMinute = 62;
  CRGBPalette16 palette = RainbowColors_p; //can adjust the palette here
  uint8_t beat = beatsin8( BeatsPerMinute, 64, 255);
  for ( int i = 0; i < NUM_LEDS; i++) { //9948
    leds[i] = ColorFromPalette(palette, gHue + (i * 2), beat - gHue + (i * 10));
  }
}
