/************************************************/
#ifndef __SSE__
# define __SSE__
#endif
#ifndef __FMA__
# define __FMA__
#endif
/************************************************/
#include "Fourier.h"
#include "Fourier_Helper.h"
/************************************************/
#if (defined(FOURIER_ALLOW_SSE) && defined(FOURIER_ALLOW_FMA))
/************************************************/

#include "Fourier_DCT2_Template.h"
void Fourier_DCT2_SSE_FMA(float *Buf, float *Tmp, int N) {
	Fourier_DCT2_Template(Buf, Tmp, N);
}

/************************************************/
#endif
/************************************************/
// EOF
/************************************************/