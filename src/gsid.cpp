/*
 * GOATTRACKER reSID interface
 */

#define GSID_C

#include <stdlib.h>
#include <stdio.h>
#include "resid/sid.h"
#include "resid-fp/sidfp.h"

#include "gsid.h"
#include "gsound.h"

int clockrate;
int samplerate;
unsigned char sidreg[NUMSIDREGS];
unsigned char sidorder[] =
  {0x18,0x17,0x16,0x15,
   0x14,0x13,0x12,0x11,0x10,0x0f,0x0e,
   0x0d,0x0c,0x0b,0x0a,0x09,0x08,0x07,
   0x06,0x05,0x04,0x03,0x02,0x01,0x00};

unsigned char altsidorder[] =
  {0x00,0x01,0x02,0x03,0x04,0x05,0x06,
   0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,
   0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,
   0x15,0x16,0x17,0x18};

SID *sid = 0;
SIDFP *sidfp = 0;

FILTERPARAMS filterparams =
  {0.50f, 3.3e6f, 1.0e-4f,
   1147036.4394268463f, 274228796.97550374f, 1.0066634233403395f, 16125.154840564108f,
   5.5f, 20.f,
   0.9613160610660189f};

extern unsigned residdelay;
//extern unsigned adparam;


void sid_init(int speed, unsigned m, unsigned ntsc, unsigned interpolate, unsigned customclockrate, unsigned usefp)
{
  int c;

  if (ntsc) clockrate = NTSCCLOCKRATE;
    else clockrate = PALCLOCKRATE;

  if (customclockrate)
    clockrate = customclockrate;

  samplerate = speed;

  if (!usefp)
  {
    if (sidfp)
    {
      delete sidfp;
      sidfp = NULL;
    }

    if (!sid) sid = new SID;
  }
  else
  {
    if (sid)
    {
      delete sid;
      sid = NULL;
    }
    
    if (!sidfp) sidfp = new SIDFP;
  }

  switch(interpolate)
  {
    case 0:
    if (sid) sid->set_sampling_parameters(clockrate, SAMPLE_FAST, speed);
    if (sidfp) sidfp->set_sampling_parameters(clockrate, SAMPLE_INTERPOLATE, speed);
    break;

    default:
    if (sid) sid->set_sampling_parameters(clockrate, SAMPLE_INTERPOLATE, speed);
    if (sidfp) sidfp->set_sampling_parameters(clockrate, SAMPLE_RESAMPLE_INTERPOLATE, speed);
    break;
  }

  if (sid) sid->reset();
  if (sidfp) sidfp->reset();
  for (c = 0; c < NUMSIDREGS; c++)
  {
    sidreg[c] = 0x00;
  }
  if (m == 1)
  {
    if (sid) sid->set_chip_model(MOS8580);
    if (sidfp) sidfp->set_chip_model(MOS8580);
  }
  else
  {
    if (sid) sid->set_chip_model(MOS6581);
    if (sidfp) sidfp->set_chip_model(MOS6581);
  }

  if (sidfp)
  {
    sidfp->get_filter().set_distortion_properties(
      filterparams.distortionrate,
      filterparams.distortionpoint,
      filterparams.distortioncfthreshold);
    sidfp->get_filter().set_type3_properties(
      filterparams.type3baseresistance,
      filterparams.type3offset,
      filterparams.type3steepness,
      filterparams.type3minimumfetresistance);
    sidfp->get_filter().set_type4_properties(
      filterparams.type4k,
      filterparams.type4b);
    sidfp->set_voice_nonlinearity(
      filterparams.voicenonlinearity);
  }
}

// Envelope level (0-255) of a voice of the emulated SID, for the level
// meters. 0 without reSID (HardSID / CatWeasel output)
int sid_envelope(int voice)
{
  if ((voice < 0) || (voice > 2)) return 0;
  if (sid) return sid->envelope_level(voice);
  if (sidfp) return sidfp->envelope_level(voice);
  return 0;
}

unsigned char sid_getorder(unsigned char index, unsigned int adparam)
{
  if (adparam >= 0xf000)
    return altsidorder[index];
  else
    return sidorder[index];
}

// Plugin builds render with these instead of sid_fillbuffer(): the player
// writes the registers once per frame (sid_newframe() after playroutine()),
// 14 cycles apart as the player on a C64 does, and sid_render() then makes
// exactly the samples asked for, however the host splits its blocks. A
// block may end in the middle of the register writes: they go on in the
// next call
static int writeindex = NUMSIDREGS;
static int gapcycles = 0;
static int badlineindex = -1;
static int fillbadline = 0;

void sid_newframe(void)
{
  writeindex = 0;
  badlineindex = residdelay ? (rand() % NUMSIDREGS) : -1;
  fillbadline = 0;
}

void sid_resetchip(void)
{
  int c;

  if (sid) sid->reset();
  if (sidfp) sidfp->reset();
  for (c = 0; c < NUMSIDREGS; c++) sidreg[c] = 0x00;
  writeindex = NUMSIDREGS;
  gapcycles = 0;
}

int sid_render(short *ptr, int samples, unsigned int adparam)
{
  int total = 0;

  if ((!sid) && (!sidfp)) return 0;
  while (samples > 0)
  {
    cycle_count d;
    int result = 0;

    if (gapcycles > 0)
    {
      // The cycles after a register write
      d = gapcycles;
      if (sid) result = sid->clock(d, ptr, samples);
      else result = sidfp->clock(d, ptr, samples);
      total += result;
      ptr += result;
      samples -= result;
      // Cycles left only when the buffer is full: the rest next time
      gapcycles = d;
      if (gapcycles > 0) break;
      continue;
    }

    if (writeindex < NUMSIDREGS)
    {
      unsigned char o;

      // Possible random badline delay once per frame
      if ((writeindex == badlineindex) && (!fillbadline))
      {
        fillbadline = 1;
        gapcycles = residdelay;
        continue;
      }
      o = sid_getorder(writeindex, adparam);
      if (sid) sid->write(o, sidreg[o]);
      else sidfp->write(o, sidreg[o]);
      writeindex++;
      gapcycles = SIDWRITEDELAY;
      continue;
    }

    // Enough cycles for the samples: the emulation stops when the buffer is
    // full and keeps its exact position between samples
    d = (cycle_count)(((long long)clockrate * samples) / samplerate) + 64;
    if (sid) result = sid->clock(d, ptr, samples);
    else result = sidfp->clock(d, ptr, samples);
    total += result;
    ptr += result;
    samples -= result;
    if (!result) break;
  }
  return total;
}

int sid_fillbuffer(short *ptr, int samples, unsigned int adparam)
{
  int tdelta;
  int tdelta2;
  int result = 0;
  int total = 0;
  int c;

  int badline = rand() % NUMSIDREGS;

  tdelta = (int)(((long long)clockrate * samples) / samplerate);
  if (tdelta <= 0) return total;

  for (c = 0; c < NUMSIDREGS; c++)
  {
    unsigned char o = sid_getorder(c,adparam);

    // Possible random badline delay once per writing
    if ((badline == c) && (residdelay))
    {
      tdelta2 = residdelay;
      if (sid) result = sid->clock(tdelta2, ptr, samples);
      if (sidfp) result = sidfp->clock(tdelta2, ptr, samples);
      total += result;
      ptr += result;
      samples -= result;
      tdelta -= residdelay;
    }

    if (sid) sid->write(o, sidreg[o]);
    if (sidfp) sidfp->write(o, sidreg[o]);

    tdelta2 = SIDWRITEDELAY;
    if (sid) result = sid->clock(tdelta2, ptr, samples);
    if (sidfp) result = sidfp->clock(tdelta2, ptr, samples);
    total += result;
    ptr += result;
    samples -= result;
    tdelta -= SIDWRITEDELAY;

    if (tdelta <= 0) return total;
  }

  if (sid) result = sid->clock(tdelta, ptr, samples);
  if (sidfp) result = sidfp->clock(tdelta, ptr, samples);
  total += result;
  ptr += result;
  samples -= result;

  // Loop extra cycles until all samples produced
  while (samples)
  {
    tdelta = (int)(((long long)clockrate * samples) / samplerate);
    if (tdelta <= 0) return total;

    if (sid) result = sid->clock(tdelta, ptr, samples);
    if (sidfp) result = sidfp->clock(tdelta, ptr, samples);
    total += result;
    ptr += result;
    samples -= result;
  }

  return total;
}

