// dualLooper.cpp
// 2-Channel Variable Speed Looper for GuitarML Funbox / Daisy Seed
// Based on Pluto by Keith Bloemer
//
// Controls:
//   Knob 1 (KNOB_1) — Loop A Speed  (-2x..+2x, noon = 1x)
//   Knob 2 (KNOB_2) — Loop B Speed  (-2x..+2x, noon = 1x)
//
//   Switch 1 LEFT   — Smooth speed transitions
//   Switch 1 CENTER — Stepped speed (fixed increments)
//   Switch 1 RIGHT  — (reserved / future)
//
//   Switch 2 LEFT   — MISO (left channel to both loopers)
//   Switch 2 RIGHT  — Stereo (L->A, R->B)
//
//   FS 1  — Loop A: tap=rec/stop/overdub, double-tap=pause, hold=clear
//   FS 2  — Loop B: tap=rec/stop/overdub, double-tap=pause, hold=clear
//
//   LED 1 — Loop A: pulse=recording, solid=playing, blink=paused
//   LED 2 — Loop B: same
//
//   Dipswitch 1 — Reverse Loop A
//   Dipswitch 2 — Reverse Loop B
//
// Loop mode is fixed to NORMAL.
// Output level is fixed at unity (no volume knobs).

#include "daisy_petal.h"
#include "daisysp.h"
#include "varSpeedLooper.h"
#include "funbox.h"

using namespace daisy;
using namespace daisysp;
using namespace funbox;

// ── Hardware ─────────────────────────────────────────────────────────────────
DaisyPetal hw;

// ── Looper buffers (SDRAM, 60 s @ 96 kHz) ────────────────────────────────────
#define MAX_SIZE (96000 * 60)
float DSY_SDRAM_BSS bufA[MAX_SIZE];
float DSY_SDRAM_BSS bufB[MAX_SIZE];

varSpeedLooper looperA, looperB;

// ── Parameters ────────────────────────────────────────────────────────────────
Parameter speedA, speedB;

// ── LEDs & oscillators for visual feedback ────────────────────────────────────
Led        led1, led2;
Oscillator led_oscA, led_oscB;
float      ledBriA, ledBriB;

// ── Footswitch state ──────────────────────────────────────────────────────────
bool pauseA    = false, pauseB    = false;
bool isPlayingA = false, isPlayingB = false;

int  dblCntA = 0, dblCntB = 0;
bool chkDblA = false, chkDblB = false;

// ── Switch / dip state ────────────────────────────────────────────────────────
int  sw1[2], sw2[2], dip[4];
bool psw1[2], psw2[2], pdip[4];
int  sw1_action = 1;   // 0=smooth, 1=stepped, 2=future
int  sw2_action = 0;   // 0=MISO,   2=Stereo

// ── Speed smoothing ───────────────────────────────────────────────────────────
float curSpeedA = 1.0f, curSpeedB = 1.0f;

// ── Speed mapping helpers ─────────────────────────────────────────────────────

// Smooth: knob 0..0.5 -> -2x..1x,  0.5..1.0 -> 1x..2x
static inline float KnobToSpeed(float v)
{
    return (v <= 0.5f) ? (v * 6.0f - 2.0f) : (v * 2.0f);
}

// Stepped: fixed speed increments
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
    lp.SetReverse(spd < 0.0f);
    lp.SetIncrementSize(fabsf(spd));
}

// ── Switch update helpers ─────────────────────────────────────────────────────

void UpdateSw1()
{
    if      (psw1[0]) sw1_action = 0;   // left   = smooth
    else if (psw1[1]) sw1_action = 2;   // right  = future
    else              sw1_action = 1;   // center = stepped
}

void UpdateSw2()
{
    if      (psw2[0]) sw2_action = 0;   // left  = MISO
    else if (psw2[1]) sw2_action = 2;   // right = Stereo
    else              sw2_action = 0;   // center defaults to MISO
}

// ── Footswitch handler (reusable per channel) ─────────────────────────────────
static void HandleFootswitch(
    varSpeedLooper &lp,
    bool  risingEdge,
    int   timeHeldMs,
    bool &pause,
    bool &isPlaying,
    int  &dblCnt,
    bool &chkDbl,
    Led  &led,
    Oscillator &osc)
{
    if(risingEdge)
    {
        if(!pause)
        {
            lp.TrigRecord();
            isPlaying = false;
            if(!lp.Recording())
            {
                led.Set(1.0f);
                isPlaying = true;
            }
        }

        if(chkDbl)
        {
            if(dblCnt <= 1000)
            {
                if(lp.Recording()) lp.TrigRecord();  // stop recording before pausing
                pause = !pause;
                osc.SetWaveform(pause ? 4 : 1);      // 4=square(blink), 1=tri(pulse)
                dblCnt = 0;
                chkDbl = false;
                led.Set(1.0f);
            }
        }
        else
        {
            chkDbl = true;
        }
    }

    // Double-tap timer
    if(chkDbl)
    {
        dblCnt++;
        if(dblCnt > 1000) { dblCnt = 0; chkDbl = false; }
    }

    // Hold to clear
    if(timeHeldMs >= 1000)
    {
        pause = false;
        osc.SetWaveform(1);
        lp.Clear();
        led.Set(0.0f);
    }
}

void UpdateButtons()
{
    HandleFootswitch(looperA,
        hw.switches[Funbox::FOOTSWITCH_1].RisingEdge(),
        (int)hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs(),
        pauseA, isPlayingA, dblCntA, chkDblA, led1, led_oscA);

    HandleFootswitch(looperB,
        hw.switches[Funbox::FOOTSWITCH_2].RisingEdge(),
        (int)hw.switches[Funbox::FOOTSWITCH_2].TimeHeldMs(),
        pauseB, isPlayingB, dblCntB, chkDblB, led2, led_oscB);
}

void UpdateSwitches()
{
    bool c1 = false, c2 = false;

    for(int i = 0; i < 2; i++)
    {
        if(hw.switches[sw1[i]].Pressed() != psw1[i]) { psw1[i] = hw.switches[sw1[i]].Pressed(); c1 = true; }
        if(hw.switches[sw2[i]].Pressed() != psw2[i]) { psw2[i] = hw.switches[sw2[i]].Pressed(); c2 = true; }
    }
    if(c1) UpdateSw1();
    if(c2) UpdateSw2();

    // Dipswitches 1 & 2 = hard reverse for A and B
    for(int i = 0; i < 4; i++)
    {
        if(hw.switches[dip[i]].Pressed() != pdip[i])
        {
            pdip[i] = hw.switches[dip[i]].Pressed();
            if(i == 0) looperA.SetReverse(pdip[0]);
            if(i == 1) looperB.SetReverse(pdip[1]);
        }
    }
}

// ── Audio callback ────────────────────────────────────────────────────────────
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();
    UpdateButtons();
    UpdateSwitches();

    // Read speed knobs
    float vspeedA = speedA.Process();
    float vspeedB = speedB.Process();

    // Compute target speeds and apply immediately in stepped mode
    float tgtSpeedA, tgtSpeedB;
    if(sw1_action == 0)
    {
        tgtSpeedA = KnobToSpeed(vspeedA);
        tgtSpeedB = KnobToSpeed(vspeedB);
    }
    else
    {
        tgtSpeedA = KnobToSteppedSpeed(vspeedA);
        tgtSpeedB = KnobToSteppedSpeed(vspeedB);
        ApplySpeed(looperA, tgtSpeedA);
        ApplySpeed(looperB, tgtSpeedB);
    }

    // Per-sample loop
    for(size_t i = 0; i < size; i++)
    {
        ledBriA = led_oscA.Process();
        ledBriB = led_oscB.Process();

        // Smooth speed transitions (smooth mode only)
        if(sw1_action == 0)
        {
            fonepole(curSpeedA, tgtSpeedA, 0.00006f);
            fonepole(curSpeedB, tgtSpeedB, 0.00006f);
            ApplySpeed(looperA, curSpeedA);
            ApplySpeed(looperB, curSpeedB);
        }

        // Input routing
        float inL = in[0][i];
        float inR = (sw2_action == 2) ? in[1][i] : in[0][i];

        // Process loopers (unity gain)
        float outA = 0.0f, outB = 0.0f;
        if(!pauseA) outA = looperA.Process(inL);
        if(!pauseB) outB = looperB.Process(inR);

        // Output routing
        if(sw2_action == 2)   // Stereo: A->L, B->R
        {
            out[0][i] = inL + outA;
            out[1][i] = inR + outB;
        }
        else                  // MISO: mix both to both channels
        {
            out[0][i] = inL + outA + outB;
            out[1][i] = inL + outA + outB;
        }
    }

    // LED visual feedback
    if(looperA.Recording())      led1.Set(ledBriA * 0.5f + 0.5f);  // pulse while recording
    else if(pauseA)               led1.Set(ledBriA * 2.0f);          // blink while paused
    // solid on = set by button handler on playback start

    if(looperB.Recording())      led2.Set(ledBriB * 0.5f + 0.5f);
    else if(pauseB)               led2.Set(ledBriB * 2.0f);

    led1.Update();
    led2.Update();
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_96KHZ);
    hw.SetAudioBlockSize(48);
    float sr = hw.AudioSampleRate();  // 96000

    // Map switch/dip pins
    sw1[0] = Funbox::SWITCH_1_LEFT;   sw1[1] = Funbox::SWITCH_1_RIGHT;
    sw2[0] = Funbox::SWITCH_2_LEFT;   sw2[1] = Funbox::SWITCH_2_RIGHT;
    dip[0] = Funbox::SWITCH_DIP_1;
    dip[1] = Funbox::SWITCH_DIP_2;
    dip[2] = Funbox::SWITCH_DIP_3;
    dip[3] = Funbox::SWITCH_DIP_4;

    for(int i = 0; i < 2; i++) psw1[i] = psw2[i] = false;
    for(int i = 0; i < 4; i++) pdip[i] = false;

    // Speed knobs only — no level knobs
    speedA.Init(hw.knob[Funbox::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
    speedB.Init(hw.knob[Funbox::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);

    // Looper A — fixed NORMAL mode
    looperA.Init(bufA, MAX_SIZE);
    looperA.SetMode(varSpeedLooper::Mode::NORMAL);
    led_oscA.Init(sr);
    led_oscA.SetFreq(1.5f);
    led_oscA.SetWaveform(1);  // triangle
    ledBriA   = 0.0f;
    pauseA    = false;
    curSpeedA = 1.0f;

    // Looper B — fixed NORMAL mode
    looperB.Init(bufB, MAX_SIZE);
    looperB.SetMode(varSpeedLooper::Mode::NORMAL);
    led_oscB.Init(sr);
    led_oscB.SetFreq(1.5f);
    led_oscB.SetWaveform(1);
    ledBriB   = 0.0f;
    pauseB    = false;
    curSpeedB = 1.0f;

    // LEDs
    led1.Init(hw.seed.GetPin(Funbox::LED_1), false);
    led1.Update();
    led2.Init(hw.seed.GetPin(Funbox::LED_2), false);
    led2.Update();

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1) { /* nothing */ }
}
