/**************************************/
//! ulc-codec: Ultra-Low-Complexity Audio Codec
//! Copyright (C) 2019, Ruben Nunez (Aikku; aik AT aol DOT com DOT au)
//! Refer to the project README file for license terms.
/**************************************/
#if defined(__AVX__) || defined(__FMA__)
# include <immintrin.h>
#endif
#if defined(__SSE__)
# include <xmmintrin.h>
#endif
/**************************************/
#include <stddef.h>
/**************************************/
#include "Fourier.h"
/**************************************/

void Fourier_MDCT(float *BufOut, const float *BufIn, float *BufLap, float *BufTmp, size_t N, size_t Overlap) {
	size_t i;
	float Ns = 1.0f / Overlap;
	const float *InLo = BufIn;
	const float *InHi = BufIn  + N;
	      float *Out  = BufOut + N/2;
	      float *Lap  = BufLap;

	//! Copy state from old block
	for(i=0;i<N/2;i++) Out[i] = Lap[i];

	//! Perform windowed lapping
#if defined(__AVX__)
	__m256 a, b;
	__m256 t0, t1;
	__m256 c, s;
	__m256 wc, ws;

	{
		float c, s;
		Fourier_SinCos(8.0f * Ns, &s, &c);
		wc = _mm256_set1_ps(c);
		ws = _mm256_set1_ps(s);
	}
	{
		a = _mm256_setr_ps(0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f);
		a = _mm256_mul_ps(a, _mm256_set1_ps(Ns));
		Fourier_SinCosAVX(a, &s, &c);
		c = _mm256_xor_ps(c, _mm256_set1_ps(-0.0f));
	}

	for(i=0;i<(N-Overlap)/2;i+=8) {
		InHi -= 8; b = _mm256_load_ps(InHi);
		a = _mm256_load_ps(InLo); InLo += 8;
		b = _mm256_xor_ps(b, _mm256_set1_ps(-0.0f));
		a = _mm256_xor_ps(a, _mm256_set1_ps(-0.0f));
		b = _mm256_shuffle_ps(b, b, 0x1B);
		b = _mm256_permute2f128_ps(b, b, 0x01);
		a = _mm256_shuffle_ps(a, a, 0x1B);
		a = _mm256_permute2f128_ps(a, a, 0x01);
		_mm256_store_ps(Lap, b); Lap += 8;
		Out -= 8; _mm256_store_ps(Out, a);
	}
	for(;i<N/2;i+=8) {
		InHi -= 8; b = _mm256_load_ps(InHi);
		a = _mm256_load_ps(InLo); InLo += 8;
		b = _mm256_shuffle_ps(b, b, 0x1B);
		b = _mm256_permute2f128_ps(b, b, 0x01);
#if defined(__FMA__)
		t0 = _mm256_mul_ps(c, a);
		t1 = _mm256_mul_ps(s, a);
		t0 = _mm256_fnmadd_ps(s, b, t0);
		t1 = _mm256_fmadd_ps (c, b, t1);
#else
		t0 = _mm256_sub_ps(_mm256_mul_ps(c, a), _mm256_mul_ps(s, b));
		t1 = _mm256_add_ps(_mm256_mul_ps(s, a), _mm256_mul_ps(c, b));
#endif
		t0 = _mm256_shuffle_ps(t0, t0, 0x1B);
		t0 = _mm256_permute2f128_ps(t0, t0, 0x01);
		Out -= 8; _mm256_store_ps(Out, t0);
		_mm256_store_ps(Lap, t1); Lap += 8;
#if defined(__FMA__)
		t0 = _mm256_mul_ps(wc, c);
		t1 = _mm256_mul_ps(ws, c);
		t0 = _mm256_fmadd_ps(ws, s, t0);
		t1 = _mm256_fmsub_ps(wc, s, t1);
#else
		t0 = _mm256_add_ps(_mm256_mul_ps(ws, s), _mm256_mul_ps(wc, c));
		t1 = _mm256_sub_ps(_mm256_mul_ps(wc, s), _mm256_mul_ps(ws, c));
#endif
		c = t0;
		s = t1;
	}
#elif defined(__SSE__)
	__m128 a, b;
	__m128 t0, t1;
	__m128 c, s;
	__m128 wc, ws;
	{
		float c, s;
		Fourier_SinCos(4.0f * Ns, &s, &c);
		wc = _mm_set1_ps(c);
		ws = _mm_set1_ps(s);
	}
	{
		a = _mm_setr_ps(0.5f, 1.5f, 2.5f, 3.5f);
		a = _mm_mul_ps(a, _mm_set1_ps(Ns));
		Fourier_SinCosSSE(a, &s, &c);
		c = _mm_xor_ps(c, _mm_set1_ps(-0.0f));
	}

	for(i=0;i<(N-Overlap)/2;i+=4) {
		InHi -= 4; b = _mm_loadr_ps(InHi);
		a = _mm_load_ps(InLo); InLo += 4;
		b = _mm_xor_ps(b, _mm_set1_ps(-0.0f));
		a = _mm_xor_ps(a, _mm_set1_ps(-0.0f));
		_mm_store_ps(Lap, b); Lap += 4;
		Out -= 4; _mm_storer_ps(Out, a);
	}
	for(;i<N/2;i+=4) {
		InHi -= 4; b = _mm_loadr_ps(InHi);
		a = _mm_load_ps(InLo); InLo += 4;
#if defined(__FMA__)
		t0 = _mm_mul_ps(c, a);
		t1 = _mm_mul_ps(s, a);
		t0 = _mm_fnmadd_ps(s, b, t0);
		t1 = _mm_fmadd_ps (c, b, t1);
#else
		t0 = _mm_sub_ps(_mm_mul_ps(c, a), _mm_mul_ps(s, b));
		t1 = _mm_add_ps(_mm_mul_ps(s, a), _mm_mul_ps(c, b));
#endif
		Out -= 4; _mm_storer_ps(Out, t0);
		_mm_store_ps(Lap, t1); Lap += 4;
#if defined(__FMA__)
		t0 = _mm_mul_ps(wc, c);
		t1 = _mm_mul_ps(ws, c);
		t0 = _mm_fmadd_ps(ws, s, t0);
		t1 = _mm_fmsub_ps(wc, s, t1);
#else
		t0 = _mm_add_ps(_mm_mul_ps(ws, s), _mm_mul_ps(wc, c));
		t1 = _mm_sub_ps(_mm_mul_ps(wc, s), _mm_mul_ps(ws, c));
#endif
		c = t0;
		s = t1;
	}
#else
	float c, s;
	float wc, ws;
	Fourier_SinCos(1.0f*Ns, &ws, &wc);
	Fourier_SinCos(0.5f*Ns, &s, &c);
	c = -c;

	for(i=0;i<(N-Overlap)/2;i++) {
		float a = *InLo++;
		float b = *--InHi;
		*--Out = -a;
		*Lap++ = -b;
	}
	for(;i<N/2;i++) {
		float a = *InLo++;
		float b = *--InHi;
		*--Out = c*a - s*b;
		*Lap++ = s*a + c*b;

		float _c =  wc*c + ws*s;
		float _s = -ws*c + wc*s;
		c = _c, s = _s;
	}
#endif
	//! Do actual transform
	Fourier_DCT4(BufOut, BufTmp, N);
}

/**************************************/
//! EOF
/**************************************/
