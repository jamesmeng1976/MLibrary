#include <math.h>  
#include <malloc.h>  
#include <string.h>
#include <stdio.h>
#include "CFftAlg.h"

CFftAlg::CFftAlg()
{
	g_datalen = 1024;
	for(int i=0;i<g_datalen;i++)
	{
		t_Data[i].re = 0.0;
		t_Data[i].im = 0.0;
		f_Data[i].im = 0.0;
		f_Data[i].re = 0.0;
		freq_fft[i] = 0.0;                                           
		Mag_fft[i] = 0.0;   
	}
    freq_mag = 100.0 ;
	Freq = 16000;
	pointer = NULL;
}
CFftAlg::~CFftAlg()
{
}
void CFftAlg::SetData(float *pointer,int dataLen)
{
	int i;
	g_datalen = dataLen;
	for(i=0;i<dataLen;i++)
	{
		t_Data[i].re = *(pointer+i);
	}	
}
TCOMPLEX  CFftAlg::ComplexAdd(TCOMPLEX c1, TCOMPLEX c2)  
{  
	TCOMPLEX c;  
	c.re = c1.re + c2.re;  
	c.im = c1.im + c2.im;  
	return c;  
}  
/*负数的减运算*/  
TCOMPLEX CFftAlg::ComplexSub(TCOMPLEX c1, TCOMPLEX c2)  
{  
	TCOMPLEX c;  
	c.re = c1.re - c2.re;  
	c.im = c1.im - c2.im;  
	return c;  
} 
/*复数的乘运算*/  
TCOMPLEX CFftAlg::ComplexMultiply(TCOMPLEX c1, TCOMPLEX c2)  
{  
	TCOMPLEX c;  
	c.re = c1.re*c2.re - c1.im*c2.im;  
	c.im = c1.re*c2.im + c1.im*c2.re;  
	return c;  
} 
/*快速傅立叶变换
TD为时域值，FD为频域值，nPower为2的幂数*/  
void CFftAlg:: FFT_N(TCOMPLEX *TD, TCOMPLEX *FD, int nPower)  
{  
	int nCount;  
	int nI,nJ,nK,nBfsize,nP;  
	double dAngle;  
	TCOMPLEX *pTw,*pTx1,*pTx2,*pTx;  
	/*计算傅立叶变换点数*/
	nCount = 1<<nPower; 
	//printf("nCount = %d\n",nCount);
	/*分配运算器所需存储器*/  
	pTw = (TCOMPLEX *)malloc(sizeof(TCOMPLEX)*nCount/2);  
	pTx1 = (TCOMPLEX *)malloc(sizeof(TCOMPLEX)*nCount);  
	pTx2 = (TCOMPLEX *)malloc(sizeof(TCOMPLEX)*nCount);  
	/*计算加权系数*/
	for(nI = 0; nI<nCount/2; nI++)  
	{  
		dAngle = -nI*PI*2/nCount;  
		pTw[nI].re = (float)cos(dAngle);  
		pTw[nI].im = (float)sin(dAngle);  
	}  
	/*将时域点写入存储器*/
	memcpy(pTx1, TD, sizeof(TCOMPLEX)*nCount);  
	/*蝶形运算*/
	for(nK=0; nK<nPower; nK++)  
	{  
		for(nJ=0;nJ<1<<nK;nJ++)  
		{  
			nBfsize = 1 << ( nPower - nK );  
			for(nI=0;nI<nBfsize/2;nI++)  
			{  
				nP=nJ*nBfsize;  
				pTx2[nI+nP] = ComplexAdd(pTx1[nI+nP], pTx1[nI+nP+nBfsize/2]);  
				pTx2[nI+nP+nBfsize/2] = ComplexMultiply(ComplexSub(pTx1[nI+nP], pTx1[nI+nP+nBfsize/2]),pTw[nI*(1<<nK)]);  
			}  
		}  
		pTx  = pTx1;  
		pTx1 = pTx2;  
		pTx2 = pTx;  
	}  
	/*重新排序*/  
	for(nJ=0;nJ<nCount;nJ++)  
	{  
		nP=0;  
		for(nI=0;nI<nPower;nI++)  
		{  
			if ( nJ&(1<<nI) )  
				nP+=1 << (nPower-nI-1);  
		}  
		FD[nJ] = pTx1[nP];
	}  
	/*释放存储器*/  
	free(pTw);  
	free(pTx1);  
	free(pTx2);  
} 
/*快速傅立叶反变换，利用快速傅立叶变换 
FD为频域值，TD为时域值，nPower为2的幂数*/  
void CFftAlg::IFFT_N(TCOMPLEX *FD, TCOMPLEX *TD, int nPower)  
{  
	int nI,nCount;  
	TCOMPLEX *pTxx;  
	/*计算傅立叶反变换点数*/
	nCount = 1<<nPower;
	/*分配运算所需存储器*/  
	pTxx = (TCOMPLEX *)malloc(sizeof(TCOMPLEX)*nCount);  
	/*将频域点写入存储器*/
	memcpy(pTxx,FD,sizeof(TCOMPLEX)*nCount);  
	/*求频域点的共轭*/
	for(nI=0; nI<nCount; nI++)  
	{  
		pTxx[nI].im = -pTxx[nI].im;  
	}  
	/*调用快速傅立叶变换*/
	FFT_N(pTxx,TD,nPower);  
	/*求时域点的共轭*/
	for(nI=0;nI<nCount;nI++)  
	{  
		TD[nI].re/=nCount;  
		TD[nI].im=-TD[nI].im/nCount;  
	}  
	/*释放存储器*/  
	free(pTxx);  
}  
void CFftAlg::DoFFT()
{
	int nPower;
	int i;
	nPower = (int)( log((g_datalen+1)*1.0)/log(2.0));
	FFT_N(t_Data, f_Data, nPower)  ;
	// 
	for(i=0;i<g_datalen;i++)
	{
		Mag_fft[i] = sqrt( f_Data[i].re*f_Data[i].re  +  f_Data[i].im*f_Data[i].im );
		freq_fft[i] = float (i*Freq/g_datalen);
		if (freq_mag<Mag_fft[i])
		{
			freq_mag = freq_fft[i];
		}
	}
}
float * CFftAlg::GetAmplitude()
{
	pointer = Mag_fft;
	return pointer;
}
float * CFftAlg::GetFreIndex()
{
	pointer = freq_fft;
	return pointer;
}
void CFftAlg::SetFreq(float freq)
{
	Freq = freq;
}
float CFftAlg::GetFreqMax()
{
	return freq_mag;
}