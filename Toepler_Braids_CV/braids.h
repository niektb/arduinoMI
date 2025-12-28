#include <sys/types.h>
#pragma once

// braids dsp

const uint16_t bit_reduction_masks[] = {
  0xffff,
  0xfff0,
  0xff00,
  0xf800,
  0xf000,
  0xe000,
  0xc000
};

#define MI_SAMPLERATE 96000.f
#define BLOCK_SIZE 32  // --> macro_oscillator.h !
#define SAMP_SCALE (float)(1.0 / 32756.0)

typedef struct
{
  braids::MacroOscillator *osc;

  float samps[BLOCK_SIZE];
  int16_t buffer[BLOCK_SIZE];
  uint8_t sync_buffer[BLOCK_SIZE];

} PROCESS_CB_DATA;

char shared_buffer[16384];

const size_t kBlockSize = BLOCK_SIZE;

struct Unit {
  braids::Quantizer *quantizer;
  braids::SignatureWaveshaper *ws;
  braids::Envelope *envelope;

  bool last_trig;

  PROCESS_CB_DATA pd;
  float *samples;
  float ratio;
};

static long src_input_callback(void *cb_data, float **audio);

struct Unit voices[1];

// Plaits modulation vars, reusing names
int16_t morph_in = 4000;  // IN(4);
float trigger_in;         //IN(5);
float level_in = 0.0f;    //IN(6);
float harm_in = 0.1f;
int16_t timbre_in = 4000;
int engine_in;
int32_t previous_pitch;
int32_t pitch_in = 60 << 7;
int16_t pitch_fm;
int16_t pitch_adj = 100;

float fm_mod = 0.0f;     //IN(7);
float timb_mod = 0.0f;   //IN(8);
float morph_mod = 0.0f;  //IN(9);
float decay_in = 0.5f;   // IN(10);
float lpg_in = 0.1f;     // IN(11);

uint8_t SETTING_AD_ATTACK = 2;
uint8_t SETTING_AD_DECAY = 7;


void updateBraidsAudio() {
  static uint16_t gain_lp;
  int16_t *buffer = voices[0].pd.buffer;
  uint8_t *sync_buffer = voices[0].pd.sync_buffer;
  size_t size = BLOCK_SIZE;

  voices[0].envelope->Update(
    SETTING_AD_ATTACK * 8,
    SETTING_AD_DECAY * 8);
  uint32_t ad_value = voices[0].envelope->Render();

  braids::MacroOscillator *osc = voices[0].pd.osc;
  osc->set_pitch(pitch_in << 7);
  osc->set_parameters(timbre_in, morph_in);

  // set shape/model
  uint8_t shape = (int)(engine_in);
  if (shape >= braids::MACRO_OSC_SHAPE_LAST)
    shape -= braids::MACRO_OSC_SHAPE_LAST;
  osc->set_shape(static_cast<braids::MacroOscillatorShape>(shape));

  bool trigger = (trigger_in != 0.0f);
  bool trigger_flag = (trigger && (!voices[0].last_trig));
  voices[0].last_trig = trigger;

  if (waitForMute) {
    if (voices[0].envelope->segment() == braids::ENV_SEGMENT_ATTACK) {
      voices[0].envelope->Trigger(braids::ENV_SEGMENT_DECAY); // Trigger Mute
    } else if (voices[0].envelope->segment() == braids::ENV_SEGMENT_DEAD) {
      muted = true; // We are muted, set the flag that EEPROM writing can start
      // Keep waitForMute = true so UI thread knows to commit EEPROM
    } 
  } else if (!waitForMute && muted) {
    // We have been unmuted, retrigger the envelope
    voices[0].envelope->Trigger(braids::ENV_SEGMENT_ATTACK);
    muted = false;
  } else if (trigger && autoTrigger) {
    osc->Strike();
    voices[0].envelope->Trigger(braids::ENV_SEGMENT_ATTACK);
    trigger_in = 0.0f;
  }

  osc->Render(sync_buffer, buffer, size);

  int32_t gain;
  if (muted) {
    gain = 0;
  } else if (autoTrigger || (waitForMute && !muted)) {
    gain = ad_value;
  } else {
    gain = 65536;
  }

  for (size_t i = 0; i < kBlockSize; ++i) {
    int16_t sample = buffer[i] * gain_lp >> 16;
    gain_lp += (gain - gain_lp) >> 4;
    buffer[i] = sample;
  }
}

// initialize macro osc
void initVoices() {

  voices[0].ratio = 48000.f / MI_SAMPLERATE;

  // init some params
  voices[0].pd.osc = new braids::MacroOscillator;
  memset(voices[0].pd.osc, 0, sizeof(*voices[0].pd.osc));

  voices[0].pd.osc->Init(48000.f);
  voices[0].pd.osc->set_pitch((48 << 7));
  voices[0].pd.osc->set_shape(braids::MACRO_OSC_SHAPE_VOWEL_FOF);

  voices[0].ws = new braids::SignatureWaveshaper;
  voices[0].ws->Init(123774);

  voices[0].quantizer = new braids::Quantizer;
  voices[0].quantizer->Init();
  voices[0].quantizer->Configure(braids::scales[0]);

  //unit->jitter_source.Init();

  memset(voices[0].pd.buffer, 0, sizeof(int16_t) * BLOCK_SIZE);
  memset(voices[0].pd.sync_buffer, 0, sizeof(voices[0].pd.sync_buffer));
  memset(voices[0].pd.samps, 0, sizeof(float) * BLOCK_SIZE);

  voices[0].last_trig = false;

  voices[0].envelope = new braids::Envelope;
  voices[0].envelope->Init();

  // get some samples initially
  updateBraidsAudio();
}

const braids::SettingsData kInitSettings = {
  braids::MACRO_OSC_SHAPE_CSAW,

  braids::RESOLUTION_16_BIT,
  braids::SAMPLE_RATE_96K,

  0,      // AD->timbre
  false,  // Trig source auto trigger
  1,      // Trig delay
  false,  // Meta modulation

  braids::PITCH_RANGE_440,
  2,
  0,  // Quantizer is off
  false,
  false,
  false,

  2,                  // Brightness
  SETTING_AD_ATTACK,  // AD attack
  SETTING_AD_DECAY,   // AD decay
  0,                  // AD->FM
  0,                  // AD->COLOR
  1,                  // AD->VCA
  0,                  // Quantizer root

  50,
  15401,
  2048,

  { 0, 0 },
  { 32768, 32768 },
  "GREETINGS FROM MUTABLE INSTRUMENTS *EDIT ME*",
};
