// L-wave firmware (Teensy 4.1) — entry point.
//
// setup(): hardware, delay memory (PSRAM if fitted), engine, stored
//          calibration, audio, then the boot-mode decision from the first scans.
// loop():  scan the panel → controller → serial console → alive blink.
//          The sound itself is rendered in the audio interrupt (EngineStream).
#include <Arduino.h>
#include <AudioStream.h>
#include <control_sgtl5000.h>
#include <output_i2s.h>
#include "lwave/controller.h"
#include "lwave/engine.h"
#include "lwave/hw_map.h"
#include "teensy_audio.h"
#include "teensy_platform.h"
#include "teensy_scanner.h"

// ---- delay memory ------------------------------------------------------------
// With PSRAM the delay line holds 20 s; without, 2.5 s in RAM2 (DMAMEM).
static constexpr float kPsramDelaySeconds = 20.5f;
static constexpr uint32_t kFallbackDelaySamples = 110400;  // 2.5 s at 44.1 kHz (220 KB)
DMAMEM static int16_t gDelayFallback[kFallbackDelaySamples];

// ---- objects -----------------------------------------------------------------
static lw::Engine gEngine;
static lw::Controller gController;
static TeensyPlatform gPlatform;
static TeensyScanner gScanner;
static lw::ScanFrame gFrame;

EngineStream gEngineStream(gEngine);
AudioOutputI2S gI2s;
AudioConnection gPatchLeft(gEngineStream, 0, gI2s, 0);
AudioConnection gPatchRight(gEngineStream, 0, gI2s, 1);
AudioControlSGTL5000 gCodec;

static char gLine[96];
static int gLineLen = 0;
static float gHpVolume = -1.0f;

static void readConsole() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (gLineLen > 0) {
        gLine[gLineLen] = 0;
        gController.onLine(gLine);
        gLineLen = 0;
      }
    } else if (gLineLen < (int)sizeof(gLine) - 1) {
      gLine[gLineLen++] = c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(lw::kOnboardLedPin, OUTPUT);
  gPlatform.begin();
  gScanner.begin();

  // Delay line: PSRAM when fitted, otherwise the RAM2 fallback.
  int16_t* delayBuf = nullptr;
  uint32_t delayLen = 0;
  if (TeensyPlatform::psramMegabytes() > 0) {
    delayLen = (uint32_t)(kPsramDelaySeconds * AUDIO_SAMPLE_RATE_EXACT);
    delayBuf = static_cast<int16_t*>(extmem_malloc(delayLen * sizeof(int16_t)));
  }
  if (!delayBuf) {
    delayBuf = gDelayFallback;
    delayLen = kFallbackDelaySamples;
  }

  gEngine.init(AUDIO_SAMPLE_RATE_EXACT, delayBuf, delayLen);
  gController.begin(gPlatform, gEngine);

  AudioMemory(12);
  gCodec.enable();
  gHpVolume = gController.settings().hpVolume;
  gCodec.volume(gHpVolume);

  // A few scans so the mux inputs settle, then decide the boot mode.
  for (int i = 0; i < 8; ++i) gScanner.scan(gFrame);
  gController.boot(gFrame);
}

void loop() {
  gScanner.scan(gFrame);
  gController.onFrame(gFrame);
  readConsole();

  float hp = gController.settings().hpVolume;
  if (hp != gHpVolume) {
    gHpVolume = hp;
    gCodec.volume(hp);
  }
  digitalWriteFast(lw::kOnboardLedPin, (millis() / 500) & 1);  // alive blink
}
