/*
  (c) 2025 blueprint@poetaster.de
  GPLv3 the libraries are MIT as the originals for STM from MI were also MIT.
*/

/* toepler +
    // Toepler Plus pins
  #define OUT1 (0u)
  #define OUT2 (1u)
  #define SW1 (8u)
  #define CV1 (26u)
  #define CV2 (27u)
  #define CV3 (28u)
*/

bool debug = false;
bool autoTrigger = true;
bool waitForMute = false;
bool muted = false;

#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel strip(2, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

#include <Arduino.h>
#include "stdio.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "potentiometer.h"
#include <EEPROM.h>

float pitch;
float pitch_offset = 36;
float freq;

float max_voltage_of_adc = 3.3;
float voltage_division_ratio = 0.3333333333333;
float notes_per_octave = 12;
float volts_per_octave = 1;
float mapping_upper_limit = 120.0;  // (max_voltage_of_adc / voltage_division_ratio) * notes_per_octave * volts_per_octave;
float mapping_lower_limit = 0.0;

#include <hardware/pwm.h>
#include <PWMAudio.h>

#define SAMPLERATE 48000
#define PWMOUT 0
#define BUTTON_PIN 8
#define LED 13

#include "utility.h"
#include <STMLIB.h>
#include <BRAIDS.h>
#include "braids.h"

#include <Bounce2.h>
Bounce2::Button button = Bounce2::Button();
bool longPress = false;

PWMAudio DAC(PWMOUT);  // 16 bit PWM audio

uint8_t engineCount = 0;
int engineInc = 0;

// clock timer  stuff
#define TIMER_INTERRUPT_DEBUG 0
#define _TIMERINTERRUPT_LOGLEVEL_ 4

// Can be included as many times as necessary, without `Multiple Definitions` Linker Error
#include "RPi_Pico_TimerInterrupt.h"


#define TIMER0_INTERVAL_MS 20.833333333333

#define DEBOUNCING_INTERVAL_MS 2  // 80
#define LOCAL_DEBUG 0

volatile int counter = 0;

// Init RPI_PICO_Timer, can use any from 0-15 pseudo-hardware timers
RPI_PICO_Timer ITimer0(0);


int favoriteEngines[] = { 1, 3, 4, 7, 8, 10, 14, 17, 28, 32, 33, 34, 35, 37, 40, 41, 42, 46 };
const int numFavoriteEngines = sizeof(favoriteEngines) / sizeof(int);

bool TimerHandler0(struct repeating_timer *t) {
  (void)t;
  bool sync = true;
  if (DAC.availableForWrite()) {
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
      DAC.write(voices[0].pd.buffer[i], sync);
    }
    counter = 1;
  }
  return true;
}

void cb() {
  bool sync = true;
  if (DAC.availableForWrite() >= BLOCK_SIZE) {
    for (int i = 0; i < BLOCK_SIZE; i++) {
      DAC.write(voices[0].pd.buffer[i]);
    }
  }
}

void voct_midi(int cv_in) {
  pitch = map(potvalue[cv_in], 0.0, 4095.0, mapping_upper_limit, mapping_lower_limit);  // convert pitch CV data value to a MIDI note number
  pitch = pitch - pitch_offset;                                         // pitch offset drops this octaves down

  pitch_in = pitch;
  if (pitch != previous_pitch) {
    trigger_in = 1.0f;
    previous_pitch = pitch;
  }
}

void setup() {

  if (debug) {
    Serial.begin(57600);
    delay(2000);
    Serial.println(F("YUP"));
  }

  EEPROM.begin(256);  // Initialize EEPROM emulation

  strip.begin();
  strip.setBrightness(50);
  setEngineIndicator();
  strip.setPixelColor(1, strip.Color(255 * autoTrigger, 0, 255 * autoTrigger));
  strip.show();

  delay(10);

  analogReadResolution(12);
  // this is to switch to PWM for power to avoid ripple noise
  pinMode(23, OUTPUT);
  digitalWrite(23, HIGH);

  pinMode(AIN0, INPUT);
  pinMode(AIN1, INPUT);
  pinMode(AIN2, INPUT);

  pinMode(LED, OUTPUT);

  button.attach(BUTTON_PIN, INPUT);
  button.interval(5);
  button.setPressedState(LOW);

  // pwm timing setup, we're using a pseudo interrupt
  if (ITimer0.attachInterruptInterval(TIMER0_INTERVAL_MS, TimerHandler0))  // that's 48kHz
  {
    if (debug) Serial.print(F("Starting  ITimer0 OK, millis() = "));
    Serial.println(millis());
  } else {
    if (debug) Serial.println(F("Can't set ITimer0. Select another freq. or timer"));
  }

  // Load saved values from EEPROM
  autoTrigger = EEPROM.read(0);
  engineCount = EEPROM.read(1);
  if (engineCount >= numFavoriteEngines) engineCount = 0;  // Ensure engineCount stays within valid range
  engine_in = favoriteEngines[engineCount];

  //Serial.println(autoTrigger);
  //Serial.println(engineCount);

  // Update LED strip based on loaded values
  strip.setPixelColor(0, strip.Color(engineCount * 5, 0, 255 - (engineCount * 5)));
  strip.setPixelColor(1, strip.Color(255 * autoTrigger, 0, 255 * autoTrigger));
  strip.show();

  // set up Pico PWM audio output
  DAC.setBuffers(4, 32);  // plaits::kBlockSize); // DMA buffers
  //DAC.onTransmit(cb);
  DAC.setFrequency(SAMPLERATE);
  DAC.begin();

  // init the braids voices
  initVoices();

  // initial reading of the pots with debounce
  readpot(0);
  readpot(1);
  readpot(2);

  int16_t timbre = (map(potvalue[0], POT_MIN, POT_MAX, 0, 32767));
  timbre_in = timbre;

  int16_t morph = (map(potvalue[1], POT_MIN, POT_MAX, 0, 32767));
  morph_in = morph;
}

void loop() {
  if (counter > 0) {
    updateBraidsAudio();
    counter = 0;  // increments on each pass of the timer after the timer writes samples
  }
}

// second core dedicated to display foo
void setup1() {
  delay(200);  // wait for main core to start up perhipherals
  if (debug) {
    delay(2000);
  }
}

// second core deals with ui / control rate updates
void loop1() {
  uint32_t now = millis();

  int16_t timbre = (map(potvalue[0], POT_MIN, POT_MAX, 0, 32767));
  timbre_in = timbre;
  int16_t morph = (map(potvalue[1], POT_MIN, POT_MAX, 0, 32767));
  morph_in = morph;

  voct_midi(2);

  button.update();

  if (button.read() == 0) {
    if (button.currentDuration() > 1000) {
      longPress = true;
    }
  }

  if (button.released()) {
    if (longPress) {
      // long press
      longPress = false;
      autoTrigger = !autoTrigger;
      strip.setPixelColor(1, strip.Color(255 * autoTrigger, 0, 255 * autoTrigger));
      strip.show();
      EEPROM.write(0, autoTrigger);
      muted = false;
      waitForMute = true;
    } else {
      // short press
      engineCount++;
      if (engineCount >= numFavoriteEngines) {
        engineCount = 0;
      }
      engine_in = favoriteEngines[engineCount];
      setEngineIndicator();
      strip.show();
      EEPROM.write(1, engineCount);
      
      muted = false;
      waitForMute = true;

    }
  }

  if (waitForMute && muted) {
    EEPROM.commit();
    waitForMute = false;
  }

  // reading A/D seems to cause noise in the audio so don't do it too often
  if ((now - pot_timer) > POT_SAMPLE_TIME) {
    readpot(0);
    readpot(1);
    readpot(2);

    pot_timer = now;
  }
}

const int brightnessIncrements = 255 / numFavoriteEngines;

void setEngineIndicator() {
  strip.setPixelColor(0, strip.Color(engineCount * brightnessIncrements, 0, 255 - (engineCount * brightnessIncrements)));
}
