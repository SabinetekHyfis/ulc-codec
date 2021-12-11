/**************************************/
//! ulc-codec: Ultra-Low-Complexity Audio Codec
//! Copyright (C) 2021, Ruben Nunez (Aikku; aik AT aol DOT com DOT au)
//! Refer to the project README file for license terms.
/**************************************/
#pragma once
/**************************************/
#include <math.h>
/**************************************/
#include "ulcEncoder.h"
#include "ulcHelper.h"
/**************************************/

//! Get optimal log base-2 overlap and window scalings for transients
//! The idea is that if a transient is relatively centered with the
//! transition region of a subblock, then we can just set the overlap
//! amount to account for it and avoid reducing the window size too
//! much, preserving the quality gains of a larger transform. At the
//! same time, we also need to /make/ the transient sit within a
//! transition region to take advantage of this, and so we combine
//! the overlap scaling and window-switching strategies.
//! NOTE: Bit codes for transient region coding, and their window sizes:
//!  First nybble:
//!   0xxx: No decimation. xxx = Overlap scaling
//!   1xxx: Decimate. xxx = Overlap scaling for the transient subblock
//!  Second nybble (when first nybble is 1xxx; otherwise, this is implicitly 0001):
//!   1xxx: Decimation by 1/8: Position = 0~7
//!    1000: N/8*,N/8,N/4,N/2
//!    1001: N/8,N/8*,N/4,N/2
//!    1010: N/4,N/8*,N/8,N/2
//!    1011: N/4,N/8,N/8*,N/2
//!    1100: N/2,N/8*,N/8,N/4
//!    1101: N/2,N/8,N/8*,N/4
//!    1110: N/2,N/4,N/8*,N/8
//!    1111: N/2,N/4,N/8,N/8*
//!   01xx: Decimation by 1/4: Position = 0~3
//!    0100: N/4*,N/4,N/2
//!    0101: N/4,N/4*,N/2
//!    0110: N/2,N/4*,N/4
//!    0111: N/2,N/4,N/4*
//!   001x: Decimation by 1/2: Position = 0~1
//!    0010: N/2*,N/2
//!    0011: N/2,N/2*
//!   0001: No decimation (not coded in the bitstream)
//!    0001: N/1*
//!  The starred subblocks set overlap scaling with regards to the last [sub]block.
//! NOTE:
//!  -TransientBuffer[] must be ULC_MAX_BLOCK_DECIMATION_FACTOR*2 in size
//!   and will be updated as (L = R_Old, R = New). Initialize with 0.
//!  -TmpBuffer[] must be BlockSize*2 in size
//!  -TransientFilter[] must be 3 elements in size
#pragma GCC push_options
#pragma GCC optimize("fast-math") //! Should improve things, hopefully, maybe
static inline void Block_Transform_GetWindowCtrl_TransientFiltering(
	const float *ThisBlockData,
	const float *LastBlockData,
	struct ULC_TransientData_t *TransientBuffer,
	      float *TransientFilter,
	      float *TmpBuffer,
	      int    BlockSize,
	      int    nChan,
	      int    RateHz
) {
	int n, Chan;

	//! Perform a highpass and bandpass filter over the signal.
	//! This removes unwanted low frequency oscillations (eg.
	//! beating) that can cause issues on transient detection,
	//! and gives us separate spectra to analyze, as in some
	//! signals, transients only have very short-lived HF data
	//! (with correspondingly longer-lived MF/LF data), while
	//! more common transients simply have a lot of HF data.
	//! Transfer functions:
	//!  Highpass: H(z) = -z^-1 + 2 - z^1 (Gain: 4.0)
	//!  Bandpass: H(z) = -z^-1     + z^1 (Gain: 2.0)
	//! The output of these filters is multiplied together as
	//! we will later use a geometric mean to combine them.
	float *BufHP = TmpBuffer;
	float *BufBP = TmpBuffer + BlockSize; {
		for(n=0;n<BlockSize;n++) BufHP[n] = BufBP[n] = 0.0f;
		for(Chan=0;Chan<nChan;Chan++) {
			int Lag = BlockSize/2; //! MDCT alignment
			float *DstHP = BufHP;
			float *DstBP = BufBP;
			const float *SrcOld = LastBlockData + Chan*BlockSize + BlockSize-Lag;
			const float *SrcNew = ThisBlockData + Chan*BlockSize;
#define FILTHP(zM1, z0, z1) *DstHP++ += SQR(-(zM1) + 2*(z0) - (z1))
#define FILTBP(zM1, z0, z1) *DstBP++ += SQR(-(zM1)          + (z1))
#define DOFILTER(zM1, z0, z1) FILTHP(zM1, z0, z1), FILTBP(zM1, z0, z1)
			for(n=0;n<Lag-1;n++) {
				DOFILTER(SrcOld[-1], SrcOld[ 0], SrcOld[+1]), SrcOld++;
			}
			{
				DOFILTER(SrcOld[-1], SrcOld[ 0], SrcNew[ 0]), SrcOld++, n++;
				DOFILTER(SrcOld[-1], SrcNew[ 0], SrcNew[+1]), SrcNew++, n++;
			}
			for(;n<BlockSize;n++) {
				DOFILTER(SrcNew[-1], SrcNew[ 0], SrcNew[+1]), SrcNew++;
			}
#undef DOFILTER
#undef FILTBP
#undef FILTHP
		}
	}

	//! Model the attack and decay envelopes, and then
	//! integrate them separately over each segment.
	{
		int i, BinSize = BlockSize / ULC_MAX_BLOCK_DECIMATION_FACTOR;
		float EnvGain = TransientFilter[0], GainRate = expf(-0x1.5A92D7p8f  / RateHz); //!  -3.0dB/ms
		float EnvAtt  = TransientFilter[1], AttRate  = expf(-0x1.5A92D7p8f  / RateHz); //!  -3.0dB/ms
		float EnvDec  = TransientFilter[2], DecRate  = expf(-0x1.5A92D7p10f / RateHz); //! -12.0dB/ms
		struct ULC_TransientData_t *Dst = TransientBuffer + ULC_MAX_BLOCK_DECIMATION_FACTOR; //! Align to new block
		const float *SrcHP = BufHP;
		const float *SrcBP = BufBP;
		i = ULC_MAX_BLOCK_DECIMATION_FACTOR; do {
			float v;
			struct ULC_TransientData_t Sum = {.Att = 0.0f, .Dec = 0.0f, .AttW = 0.0f, .DecW = 0.0f};
			n = BinSize; do {
				//! Accumulate and subtract the "drift" bias
				//! (ie. the amplitude at which everything is
				//! happening around) to "unbias" the signal
				//! and center it about 0 for attack/decay.
				v        = sqrtf(sqrtf((*SrcHP++) * (*SrcBP++))); //! Pow2Amp[GeometricMean[{HP,BP}]] = Sqrt[Sqrt[HP*BP]]
				EnvGain += (v - EnvGain)*(1.0f-GainRate);
				v       -= EnvGain;

				//! Update the attack/decay curves and integrate each
				//! as the contraharmonic mean power over each segment.
				//! Using a contraharmonic mean allows us to catch
				//! onsets a lot more cleanly when they're "on the edge".
				//! NOTE: EnvDec is sign-inverted.
				//! NOTE: Envelope attack (fade-in) is proportional to
				//! the signal level, whereas envelope decay (fade-out)
				//! follows a constant falloff.
				if(v >= EnvAtt) EnvAtt += (v-EnvAtt)*(1.0f-AttRate); else EnvAtt *= AttRate;
				if(v <= EnvDec) EnvDec += (v-EnvDec)*(1.0f-DecRate); else EnvDec *= DecRate;
				v = SQR(EnvAtt); Sum.Att += SQR(v), Sum.AttW += v;
				v = SQR(EnvDec); Sum.Dec += SQR(v), Sum.DecW += v;
			} while(--n);

			//! Swap out the old "new" data, and replace with new "new" data
			Dst[-ULC_MAX_BLOCK_DECIMATION_FACTOR] = *Dst;
			*Dst++ = Sum;
		} while(--i);
		TransientFilter[0] = EnvGain;
		TransientFilter[1] = EnvAtt;
		TransientFilter[2] = EnvDec;
	}
}
#pragma GCC pop_options
static inline int Block_Transform_GetWindowCtrl(
	const float *ThisBlockData,
	const float *LastBlockData,
	struct ULC_TransientData_t *TransientBuffer,
	      float *TransientFilter,
	      float *TmpBuffer,
	      int    BlockSize,
	      int    nChan,
	      int    RateHz
) {
	int n;

	//! Perform filtering to obtain transient analysis
	//! then seek to this "new" block's transient data
	//! (which is centered between the two blocks).
	Block_Transform_GetWindowCtrl_TransientFiltering(ThisBlockData, LastBlockData, TransientBuffer, TransientFilter, TmpBuffer, BlockSize, nChan, RateHz);
	TransientBuffer += ULC_MAX_BLOCK_DECIMATION_FACTOR;

	//! Keep decimating until no pre-/post-echo is detected in any segment
	int   Log2SubBlockSize = 31 - __builtin_clz(BlockSize);
	int   Decimation     = 0b0001;
	float TransientRatio = 0.0f; {
		int nSegments   = 1;
		int SegmentSize = ULC_MAX_BLOCK_DECIMATION_FACTOR;
		for(;;) {
			//! Get the maximum Attack or Decay ratio for all segments
			//! NOTE: The ratio is calculated as AttR/DecL multiplied
			//! by AttR/AttL. The former is the most important ratio
			//! as it describes how much pre-echo we will have, while
			//! the latter just serves to clean things up when attack
			//! swells and takes a while to decay (this can cause the
			//! segment after the onset to be chosen instead, which
			//! can cause annoying pre-echo glitching). Conveniently,
			//! this latter ratio also ensures that decay transients
			//! are considered "less important" than attack.
			int Segment;
			int MaxSegment = 0; float MaxRatio = -1000.0f;
			for(Segment=0;Segment<nSegments;Segment++) {
				struct ULC_TransientData_t L = {.Att = 0.0f, .Dec = 0.0f, .AttW = 0.0f, .DecW = 0.0f};
				struct ULC_TransientData_t R = {.Att = 0.0f, .Dec = 0.0f, .AttW = 0.0f, .DecW = 0.0f};
				const struct ULC_TransientData_t *Src = TransientBuffer + Segment*SegmentSize;
				for(n=0;n<SegmentSize;n++) {
#define ADDSEGMENT(Dst, Src) Dst.Att += Src.Att, Dst.Dec += Src.Dec, Dst.AttW += Src.AttW, Dst.DecW += Src.DecW
					ADDSEGMENT(L, Src[-1-n]);
					ADDSEGMENT(R, Src[   n]);
#undef ADDSEGMENT
				}
				L.Att = L.Att ? logf(L.Att / L.AttW) : (-100.0f); //! -100 = Placeholder for Log[0]
				L.Dec = L.Dec ? logf(L.Dec / L.DecW) : (-100.0f);
				R.Att = R.Att ? logf(R.Att / R.AttW) : (-100.0f);
				float Ratio  = ABS(R.Att - L.Dec) + (R.Att - L.Att);
				if(Ratio > MaxRatio) MaxSegment = Segment, MaxRatio = Ratio;
			}
			MaxRatio *= 0x1.715476p0f; //! 0x1.715476p0 = 1/Log[2] for change of base

			//! If no transients are detected, break out and use last values
			//! NOTE: 2.0 is the threshold for SubBlockSize=2048 and it scales
			//! proportionally to the subblock size.
			float Thres = 1 + 11-Log2SubBlockSize; //! Log2[2.0 * 2048/SubBlockSize]
			if(MaxRatio < Thres) break;

			//! Set new decimation pattern + transient ratio, and continue
			//! NOTE: Also cut off at SubBlockSize=64. This should be plenty.
			Decimation     = nSegments + MaxSegment;
			TransientRatio = MaxRatio - Thres;
			if(SegmentSize > 1 && Log2SubBlockSize > 6) {
				//! Increase temporal resolution
				nSegments   *= 2;
				SegmentSize /= 2;
				Log2SubBlockSize--;
			} else break;
		}
	}

	//! Determine overlap size from the ratio
	int OverlapScale = (TransientRatio < 0.5f) ? 0 : (TransientRatio >= 6.5f) ? 7 : (int)(TransientRatio+0.5f);
	if(Log2SubBlockSize-OverlapScale < 4) OverlapScale = Log2SubBlockSize-4; //! Minimum 16-sample overlap

	//! Return the combined overlap+window switching parameters
	return OverlapScale + 0x8*(Decimation != 1) + 0x10*Decimation;
}

/**************************************/
//! EOF
/**************************************/
