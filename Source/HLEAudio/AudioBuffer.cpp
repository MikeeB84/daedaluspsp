/*
Copyright (C) 2007 StrmnNrmn

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "Base/Types.h"


#include "Interface/ConfigOptions.h"
#include "Debug/DBGConsole.h"
#include "HLEAudio/AudioBuffer.h"
#include "System/Thread/Thread.h"
#include <cstring>
#include <fstream>

#include "SysPSP/Utility/CacheUtil.h"

namespace
{
	// When the buffer runs low (the game is running below full speed), audio is
	// stretched by up to this much (12 bit fixed point, 1536 = 37.5%) instead of
	// running dry, which lowers the pitch slightly rather than crackling.
	const s32 kMaxStretch = 1536;

	// Fade length (in output samples) used when the buffer runs dry and when
	// sound resumes, so gaps don't click.
	const u32 kFadeSamples = 128;

	inline s16 Lerp( s16 a, s16 b, s32 frac )
	{
		return s16( a + ( ( ( s32( b ) - s32( a ) ) * frac ) >> 12 ) );
	}
}

CAudioBuffer::CAudioBuffer(u32 buffer_size)
    : mBufferBegin(new Sample[buffer_size]),
      mBufferEnd(mBufferBegin + buffer_size), mReadPtr(mBufferBegin),
      mWritePtr(mBufferBegin), mResamplePos(0), mFadeIn(0) {
  mLastInput.L = mLastInput.R = 0;
  mLastOutput.L = mLastOutput.R = 0;
}

CAudioBuffer::~CAudioBuffer() { delete[] mBufferBegin; }

u32 CAudioBuffer::GetNumBufferedSamples() const {
  // Safe? What if we read mWrite, and then mRead moves to start of buffer?
  s32 diff = mWritePtr - mReadPtr;

  if (diff < 0) {
    diff += (mBufferEnd - mBufferBegin); // Add on buffer length
  }

  return diff;
}

void CAudioBuffer::AddSamples(const Sample *samples, u32 num_samples,
                              u32 frequency, u32 output_freq) {
#ifdef DAEDALUS_ENABLE_ASSERTS
  DAEDALUS_ASSERT(frequency <= output_freq, "Input frequency is too high");
#endif

  if (num_samples == 0 || frequency == 0 || output_freq == 0)
    return;

#ifdef DAEDALUS_DEBUG_AUDIO
std::ofstream fh;
 
 if (!fh.is_open())
 {
  fh.open("audio_in.raw",  std::ios::binary);
fh.write(reinterpret_cast<const char*>(samples), sizeof(Sample) * num_samples);
fh.flush();
 }
#endif 
  const Sample *read_ptr(
      mReadPtr); // No need to invalidate, as this is uncached/volatile
  Sample *write_ptr(mWritePtr);

  //
  //	Linear interpolation from the input rate to the output rate, in 12 bit
  //	fixed point. The position carries over between calls (and interpolation
  //	starts from the last sample of the previous call), so the output is one
  //	continuous stream instead of restarting - and clicking - every call.
  //
  //	Position 0 is the last sample of the previous call, (i + 1) << 12 is
  //	samples[i]. 'step' is how far to move through the input per output sample.
  //
  s32 step = s32((frequency << 12) / output_freq);

  // Buffer running low: produce more output per input sample (slightly lower
  // pitch) so playback doesn't run dry while the game is below full speed.
  const s32 capacity = mBufferEnd - mBufferBegin;
  const s32 target = capacity / 2;
  const s32 buffered = s32(GetNumBufferedSamples());
  if (buffered < target) {
    s32 stretch = 4096 + ((target - buffered) * kMaxStretch) / target;
    step = (step * 4096) / stretch;
  }
  if (step < 1)
    step = 1;

  const u32 end_pos = num_samples << 12;
  u32 pos = mResamplePos;

  while (pos < end_pos) {
    const u32 idx = pos >> 12;
    const s32 frac = s32(pos & 4095);
    const Sample &a = idx == 0 ? mLastInput : samples[idx - 1];
    const Sample &b = samples[idx];

    Sample out;
    out.L = Lerp(a.L, b.L, frac);
    out.R = Lerp(a.R, b.R, frac);
    pos += step;

    write_ptr++;
    if (write_ptr >= mBufferEnd)
      write_ptr = mBufferBegin;

    while (write_ptr == read_ptr) {
      // The buffer is full - spin until the read pointer advances.
      // This locks the emulation speed to the playback rate when the game
      // is running fast.
      read_ptr = mReadPtr;
    }

    *write_ptr = out;
  }

  mResamplePos = pos - end_pos;
  mLastInput = samples[num_samples - 1];

  mWritePtr = write_ptr;
}

u32 CAudioBuffer::Drain(Sample *samples, u32 num_samples) {
  const Sample *read_ptr(mReadPtr); // No need to invalidate, as this is uncached/volatile
  const Sample *write_ptr(mWritePtr);

  Sample *out_ptr(samples);
  u32 samples_required(num_samples);

  while (samples_required > 0) {
    // Check if empty
    if (read_ptr == write_ptr)
      break;

    // AddSamples writes to the slot after the write pointer, then advances it
    read_ptr++;
    if (read_ptr >= mBufferEnd)
      read_ptr = mBufferBegin;

    Sample s = *read_ptr;
    if (mFadeIn < kFadeSamples) {
      // Resuming after running dry: fade back in instead of jumping
      s.L = s16((s32(s.L) * s32(mFadeIn)) / s32(kFadeSamples));
      s.R = s16((s32(s.R) * s32(mFadeIn)) / s32(kFadeSamples));
      mFadeIn++;
    }
    *out_ptr++ = s;
    mLastOutput = s;

    samples_required--;
  }

#ifdef DAEDALUS_DEBUG_AUDIO
std::ofstream fh;

 if (!fh.is_open())
 {
  fh.open("audio_out.raw",  std::ios::binary);
  fh.write(reinterpret_cast<const char*>(samples), sizeof(Sample) * num_samples - samples_required);
  fh.flush();
 }
#endif 
  mReadPtr = read_ptr; // No need to invalidate, as this is uncached

  //
  //	Ran dry: fade out from the last sample played rather than dropping
  //	straight to zero (which clicks), then silence. The next samples fade in.
  //
  if (samples_required > 0) {
    const u32 fade = samples_required < kFadeSamples ? samples_required : kFadeSamples;
    for (u32 i = 0; i < fade; ++i) {
      const s32 level = s32(fade - 1 - i);
      out_ptr->L = s16((s32(mLastOutput.L) * level) / s32(fade));
      out_ptr->R = s16((s32(mLastOutput.R) * level) / s32(fade));
      out_ptr++;
    }
    if (samples_required > fade)
      memset(out_ptr, 0, (samples_required - fade) * sizeof(Sample));

    mLastOutput.L = mLastOutput.R = 0;
    mFadeIn = 0;
  }

  // Return the number of samples written
  return num_samples - samples_required;
}
