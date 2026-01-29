/***********
类名：CFftAlg.h  
时间：2021.4.13
描述：傅立叶正变化
测试：经过测试，和matalb 中fft结果是一致的；
************/
#ifndef _FFT_H_
#define _FFT_H_
typedef struct 
{  
	float re;  
	float im;  
}TCOMPLEX;
/*复数的加运算*/  
#define  PI    3.1415926535897932384626433832795028841971
#endif

class CFftAlg
{
public:

	void SetData( float *pointer,int dataLen);                    // 
	void SetFreq(float freq);
	void DoFFT();
	float * GetAmplitude();
	float * GetFreIndex();
	float GetFreqMax();
	float freq_fft[1024];                                           
	float Mag_fft[1024];   
	float freq_mag;
	CFftAlg();
	~CFftAlg();

protected:

private:
	int g_datalen;
	float Freq;
	TCOMPLEX t_Data[1024];
	TCOMPLEX f_Data[1024];
	float *pointer;
	TCOMPLEX ComplexAdd(TCOMPLEX c1, TCOMPLEX c2) ;
	TCOMPLEX ComplexSub(TCOMPLEX c1, TCOMPLEX c2) ;
	TCOMPLEX ComplexMultiply(TCOMPLEX c1, TCOMPLEX c2) ;
	void FFT_N(TCOMPLEX *TD, TCOMPLEX *FD, int nPower)  ;
	void IFFT_N(TCOMPLEX *FD, TCOMPLEX *TD, int nPower) ;
};