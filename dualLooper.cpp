// dualLooper.cpp
// 2-Channel Variable Speed Looper for bare Daisy Seed
//
// ┌─────────────────────────────────────────────────────────┐
// │  PIN ASSIGNMENTS  (all Daisy Seed pin numbers)          │
// │                                                         │
// │  ANALOG IN                                              │
// │    PIN_ADC_SPEED_A  = A0 (PIN 15)  — Loop A speed knob  │
// │    PIN_ADC_SPEED_B  = A1 (PIN 16)  — Loop B speed knob  │
// │                                                         │
// │  DIGITAL IN (active-low, internal pull-up)              │
// │    PIN_FS_A         = D0 (PIN 0)   — Footswitch A       │
// │    PIN_FS_B         = D1 (PIN 1)   — Footswitch B       │
// │    PIN_SW_SMOOTH    = D2 (PIN 2)   — Speed mode toggle  │
// │                        (open=stepped, GND=smooth)       │
// │    PIN_SW_REVERSE_A = D3 (PIN 3)   — Reverse loop A     │
// │    PIN_SW_REVERSE_B = D4 (PIN 4)   — Reverse loop B     │
// │                                                         │
// │  DIGITAL OUT                                            │
// │    PIN_LED_A        = D25 (PIN 25) — LED loop A         │
// │    PIN_LED_B        = D26 (PIN 26) — LED loop B         │
// │                                                         │
// │  AUDIO                                                  │
// │    Audio In  Left   — Loop A input                      │
// │    Audio In  Right  — Loop B input                      │
// │    Audio Out Left   — Loop A + Loop B + dry L (stereo)  │
// │    Audio Out Right  — Loop A + Loop B + dry R (stereo)  │
// └─────────────────────────────────────────────────────────┘
//
// Footswitch behaviour (per channel):
//   Single tap  — start recording / stop recording & play / overdub
//   Double tap  — pause / resume playback
//   Hold 1 s    — clear loop
//
// LED behaviour:
//   Pulsing     — recording
//   Solid on    — playing
//   Blinking    — paused
//   Off         — empty
//
// Speed knob:
//   0 (full CCW)  → -2x (reverse double speed)
//   noon (0.5)    →  1x (normal forward)
//   1 (full CW)   → +2x (forward double speed)
//   SW_SMOOTH closed → smooth continuous transitions
//   SW_SMOOTH open   → stepped fixed increments

#include "daisy_seed.h"
#include "daisysp.h"
#include "varSpeedLooper.h"

using namespace daisy;
using namespace daisysp;

// ── Pin definitions ───────────────────────────────────────────────────────────
static constexpr Pin PIN_ADC_SPEED_A  = seed::A0;   // PIN 15
static constexpr Pin PIN_ADC_SPEED_B  = seed::A1;   // PIN 16

static constexpr Pin PIN_FS_A         = seed::D0;   // PIN 0
static constexpr Pin PIN_FS_B         = seed::D1;   // PIN 1
static constexpr Pin PIN_SW_SMOOTH    = seed::D2;   // PIN 2  (GND = smooth)
static constexpr Pin PIN_SW_REVERSE_A = seed::D3;   // PIN 3  (GND = reverse)
static constexpr Pin PIN_SW_REVERSE_B = seed::D4;   // PIN 4  (GND = reverse)

static constexpr Pin PIN_LED_A        = seed::D25;  // PIN 25
static constexpr Pin PIN_LED_B        = seed::D26;  // PIN 26

// ── Hardware ──────────────────────────────────────────────────────────────────
DaisySeed hw;

// ── Looper buffers  (SDRAM, 60 s @ 96 kHz) ───────────────────────────────────
#define MAX_SIZE (96000 * 60)
float DSY_SDRAM_BSS bufA[MAX_SIZE];
float DSY_SDRAM_BSS bufB[MAX_SIZE];

varSpeedLooper looperA, looperB;

// ── GPIO objects ──────────────────────────────────────────────────────────────
Switch  fsA, fsB;           // footswitches
Switch  swSmooth;           // speed mode toggle
Switch  swRevA, swRevB;     // reverse toggles
GPIO    ledPinA, ledPinB;   // LED outputs

// ── LED oscillators ───────────────────────────────────────────────────────────
Oscillator led_oscA, led_oscB;
float      ledBriA = 0.f, ledBriB = 0.f;

// ── ADC ───────────────────────────────────────────────────────────────────────
AnalogControl ctrlA, ctrlB;
Parameter speedA, speedB;

// ── Looper state ──────────────────────────────────────────────────────────────
bool pauseA     = false, pauseB     = false;
bool isPlayingA = false, isPlayingB = false;

int  dblCntA = 0,  dblCntB = 0;
bool chkDblA = false, chkDblB = false;

// ── Speed smoothing ───────────────────────────────────────────────────────────
float curSpeedA = 1.f, curSpeedB = 1.f;

// ── LED helpers (manual PWM via oscillator value) ─────────────────────────────
// We drive the LED pin high/low based on the oscillator brightness threshold.
static inline void SetLed(GPIO &pin, float brightness)
{
    // Simple threshold: >0.5 = on.  Gives a natural blink/pulse from the osc.
    pin.Write(brightness > 0.5f);
}

// ── Speed mapping ─────────────────────────────────────────────────────────────
static inline float KnobToSpeed(float v)
{
    // 0..0.5 → -2x..1x,   0.5..1.0 → 1x..2x
    return (v <= 0.5f) ? (v * 6.f - 2.f) : (v * 2.f);
}

static inline float KnobToSteppedSpeed(float v)
{
    if(v < 0.05f) return -2.0f;
    if(v < 0.15f) return -1.5f;
    if(v < 0.25f) return -1.0f;
    if(v < 0.35f) return -0.5f;
    if(v < 0.45f) return  0.5f;
    if(v < 0.55f) return  1.0f;
    if(v < 0.70f) return  1.5f;
    if(v < 0.80f) return  2.0f;
    if(v < 0.90f) return  2.5f;
    return                3.0f;
}

static inline void ApplySpeed(varSpeedLooper &lp, float spd)
{
    lp.SetReverse(spd < 0.f);
    lp.SetIncrementSize(fabsf(spd));
}

// ── Footswitch handler ────────────────────────────────────────────────────────
static void HandleFootswitch(
    varSpeedLooper &lp,
    Switch         &sw,
    bool           &pause,
    bool           &isPlaying,
    int            &dblCnt,
    bool           &chkDbl,
    GPIO           &ledPin,
    Oscillator     &osc)
{
    bool rising   = sw.RisingEdge();
    int  heldMs   = (int)sw.TimeHeldMs();

    if(rising)
    {
        if(!pause)
        {
            lp.TrigRecord();
            isPlaying = false;
            if(!lp.Recording())
            {
                ledPin.Write(true);
                isPlaying = true;
            }
        }

        if(chkDbl)
        {
            if(dblCnt <= 1000)
            {
                if(lp.Recording()) lp.TrigRecord();   // stop rec before pausing
                pause = !pause;
                osc.SetWaveform(pause ? 4 : 1);        // 4=square blink, 1=tri pulse
                dblCnt = 0;
                chkDbl = false;
                ledPin.Write(true);
            }
        }
        else
        {
            chkDbl = true;
        }
    }

    // Double-tap window timer
    if(chkDbl)
    {
        dblCnt++;
        if(dblCnt > 1000) { dblCnt = 0; chkDbl = false; }
    }

    // Hold ≥ 1 s → clear
    if(heldMs >= 1000)
    {
        pause = false;
        osc.SetWaveform(1);
        lp.Clear();
        ledPin.Write(false);
    }
}

// ── Audio callback ────────────────────────────────────────────────────────────
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    // Debounce all switches
    fsA.Debounce();
    fsB.Debounce();
    swSmooth.Debounce();
    swRevA.Debounce();
    swRevB.Debounce();

    // Footswitch handlers
    HandleFootswitch(looperA, fsA, pauseA, isPlayingA,
                     dblCntA, chkDblA, ledPinA, led_oscA);
    HandleFootswitch(looperB, fsB, pauseB, isPlayingB,
                     dblCntB, chkDblB, ledPinB, led_oscB);

    // Reverse toggles (active-low: Pressed() == GND connected)
    looperA.SetReverse(swRevA.Pressed());
    looperB.SetReverse(swRevB.Pressed());

    // Speed mode: smooth when switch is held/closed, stepped otherwise
    bool smoothMode = swSmooth.Pressed();

    // Read ADC speed knobs (0..1)
    float vspeedA = speedA.Process();
    float vspeedB = speedB.Process();

    float tgtSpeedA, tgtSpeedB;
    if(smoothMode)
    {
        tgtSpeedA = KnobToSpeed(vspeedA);
        tgtSpeedB = KnobToSpeed(vspeedB);
    }
    else
    {
        tgtSpeedA = KnobToSteppedSpeed(vspeedA);
        tgtSpeedB = KnobToSteppedSpeed(vspeedB);
        // Apply immediately in stepped mode (outside per-sample loop)
        ApplySpeed(looperA, tgtSpeedA);
        ApplySpeed(looperB, tgtSpeedB);
    }

    // Per-sample processing
    for(size_t i = 0; i < size; i++)
    {
        ledBriA = led_oscA.Process();
        ledBriB = led_oscB.Process();

        // Smooth speed glide
        if(smoothMode)
        {
            fonepole(curSpeedA, tgtSpeedA, 0.00006f);
            fonepole(curSpeedB, tgtSpeedB, 0.00006f);
            ApplySpeed(looperA, curSpeedA);
            ApplySpeed(looperB, curSpeedB);
        }

        float inL = in[0][i];   // Loop A source  (left  / mono)
        float inR = in[1][i];   // Loop B source  (right / mono)

        float outA = 0.f, outB = 0.f;
        if(!pauseA) outA = looperA.Process(inL);
        if(!pauseB) outB = looperB.Process(inR);

        // Both loopers mixed to both channels — full stereo output
        out[0][i] = inL + outA + outB;   // left  = dry L + loop A + loop B
        out[1][i] = inR + outA + outB;   // right = dry R + loop A + loop B
    }

    // LED visual feedback (applied after sample loop)
    if(looperA.Recording())      SetLed(ledPinA, ledBriA * 0.5f + 0.5f);  // pulse
    else if(pauseA)               SetLed(ledPinA, ledBriA);                 // blink
    // solid on is handled by Write(true) in HandleFootswitch

    if(looperB.Recording())      SetLed(ledPinB, ledBriB * 0.5f + 0.5f);
    else if(pauseB)               SetLed(ledPinB, ledBriB);
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_96KHZ);
    hw.SetAudioBlockSize(48);
    float sr = hw.AudioSampleRate();  // 96000

    // ── ADC setup ─────────────────────────────────────────────────────────────
    AdcChannelConfig adcCfg[2];
    adcCfg[0].InitSingle(PIN_ADC_SPEED_A);
    adcCfg[1].InitSingle(PIN_ADC_SPEED_B);
    hw.adc.Init(adcCfg, 2);
    hw.adc.Start();

    ctrlA.Init(hw.adc.GetPtr(0), hw.AudioCallbackRate());
    ctrlB.Init(hw.adc.GetPtr(1), hw.AudioCallbackRate());
    speedA.Init(ctrlA, 0.f, 1.f, Parameter::LINEAR);
    speedB.Init(ctrlB, 0.f, 1.f, Parameter::LINEAR);

    // ── Switch / button setup ─────────────────────────────────────────────────
    // Switch::Init(pin, sample_rate, type, polarity, pull)
    // SWITCH_TYPE_MOMENTARY, active-low (INPUT_PULLUP), 1 kHz update rate
    float swRate = 1000.f;  // debounce update rate (Hz), called once per block
    fsA.Init(PIN_FS_A,         swRate, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED, GPIO::Pull::PULLUP);
    fsB.Init(PIN_FS_B,         swRate, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED, GPIO::Pull::PULLUP);
    swSmooth.Init(PIN_SW_SMOOTH,    swRate, Switch::TYPE_TOGGLE,    Switch::POLARITY_INVERTED, GPIO::Pull::PULLUP);
    swRevA.Init(PIN_SW_REVERSE_A,   swRate, Switch::TYPE_TOGGLE,    Switch::POLARITY_INVERTED, GPIO::Pull::PULLUP);
    swRevB.Init(PIN_SW_REVERSE_B,   swRate, Switch::TYPE_TOGGLE,    Switch::POLARITY_INVERTED, GPIO::Pull::PULLUP);

    // ── LED GPIO setup ────────────────────────────────────────────────────────
    ledPinA.Init(PIN_LED_A, GPIO::Mode::OUTPUT);
    ledPinB.Init(PIN_LED_B, GPIO::Mode::OUTPUT);
    ledPinA.Write(false);
    ledPinB.Write(false);

    // ── LED oscillators ───────────────────────────────────────────────────────
    led_oscA.Init(sr);
    led_oscA.SetFreq(1.5f);
    led_oscA.SetWaveform(Oscillator::WAVE_TRI);

    led_oscB.Init(sr);
    led_oscB.SetFreq(1.5f);
    led_oscB.SetWaveform(Oscillator::WAVE_TRI);

    // ── Loopers ───────────────────────────────────────────────────────────────
    looperA.Init(bufA, MAX_SIZE);
    looperA.SetMode(varSpeedLooper::Mode::NORMAL);
    pauseA    = false;
    curSpeedA = 1.f;

    looperB.Init(bufB, MAX_SIZE);
    looperB.SetMode(varSpeedLooper::Mode::NORMAL);
    pauseB    = false;
    curSpeedB = 1.f;

    // ── Start audio ───────────────────────────────────────────────────────────
    hw.StartAudio(AudioCallback);

    while(1) { /* nothing to poll */ }
}
