//#pragma once
#ifndef __SYNTH_H__
#define __SYNTH_H__

#define PI_M 3.14159265358979323846
#define	SYNTH_NOTE_OFFSET	51
#define SND_NOTE_DT        (1.059463100070972f)
#define SND_NOTE_BASE      (8.0f / (float)SND_SAMPLES)
#define SND_SAMPLES        (44100)
#define SND_BASS		27.500


//	譜面の置換

//#define  k0a 0x00
//#define  k0as 0x01
//#define  k0b 0x02
#define  k1c 0x03
#define  k1cs 0x04
#define  k1d 0x05
#define  k1ds 0x06
#define  k1e 0x07
#define  k1f 0x08
#define  k1fs 0x09
#define  k1g 0x0A
#define  k1gs 0x0B
#define  k1a 0x0C
#define  k1as 0x0D
#define  k1b 0x0E
#define  k2c 0x0F
#define  k2cs 0x10
#define  k2d 0x11
#define  k2ds 0x12
#define  k2e 0x13
#define  k2f 0x14
#define  k2fs 0x15
#define  k2g 0x16
#define  k2gs 0x17
#define  k2a 0x18
#define  k2as 0x19
#define  k2b 0x1A
#define  k3c 0x1B
#define  k3cs 0x1C
#define  k3d 0x1D
#define  k3ds 0x1E
#define  k3e 0x1F
#define  k3f 0x20
#define  k3fs 0x21
#define  k3g 0x22
#define  k3gs 0x23
#define  k3a 0x24
#define  k3as 0x25
#define  k3b 0x26
#define  k4c 0x27
#define  k4cs 0x28
#define  k4d 0x29
#define  k4ds 0x2A
#define  k4e 0x2B
#define  k4f 0x2C
#define  k4fs 0x2D
#define  k4g 0x2E
#define  k4gs 0x2F
#define  k4a 0x30
#define  k4as 0x31
#define  k4b 0x32
#define  k5c 0x33
#define  k5cs 0x34
#define  k5d 0x35
#define  k5ds 0x36
#define  k5e 0x37
#define  k5f 0x38
#define  k5fs 0x39
#define  k5g 0x3A
#define  k5gs 0x3B
#define  k5a 0x3C
#define  k5as 0x3D
#define  k5b 0x3E
#define  k6c 0x3F
#define  k6cs 0x40
#define  k6d 0x41
#define  k6ds 0x42
#define  k6e 0x43
#define  k6f 0x44
#define  k6fs 0x45
#define  k6g 0x46
#define  k6gs 0x47
#define  k6a 0x48
#define  k6as 0x49
#define  k6b 0x4A
#define  k7c 0x4B
#define  k7cs 0x4C
#define  k7d 0x4D
#define  k7ds 0x4E
#define  k7e 0x4F
#define  k7f 0x50
#define  k7fs 0x51
#define  k7g 0x52
#define  k7gs 0x53
#define  k7a 0x54
#define  k7as 0x55
#define  k7b 0x56
#define  k8c 0x57

#define K_END 0x58

/*
	べき乗計算使うのではなく、乗算のみで対応。
*/
float mGetFrq(char c)
{
	
	float dt = SND_BASS;
	for (int i = 0; i < c; i++) {
		dt = dt* SND_NOTE_DT;
	}
	return dt;
}

/*
	IA32-x87命令、整数値に値を丸める。
*/
double snd_round(float x) {
	__asm {
		fld  dword ptr[x]
		frndint
	}
}

/*
	rand命令の自前実装。
*/
float f_rand() {
	static int a = 1;
	static int b = 234567;
	static int c = 890123;
	a += b; b += c; c += a;
	return (float)(a >> 16) * (1.0f / 32768.0f);
}

float Filter(float* input, float *output, int t,
			 const char type, float cutoff_freq, float q )
{

	float omega = 2.f*PI_M*cutoff_freq / 44100.f;	//角度で出す。
	float alpha = sinf(omega) / (2.0*q);

	static float temp;
	float in_1, in_2, out_1, out_2, in_0;
	float a0, a1, a2, b0, b1, b2;

	switch (type) {
	case 1:	//LPF
		a0 = 1.0f + alpha;
		a1 = -2.0f * cosf(omega);
		a2 = 1.0f - alpha;
		b0 = (1.0f - cosf(omega)) / 2.0f;
		b1 = 1.0f - cosf(omega);
		b2 = (1.0f - cosf(omega)) / 2.0f;
		break;
	case 2:	//HPF
		a0 = 1.0 + alpha;
		a1 = -2.0 * cosf(omega);
		a2 = 1.0 - alpha;
		b0 = (1.0 + cosf(omega)) / 2.0;
		b1 = -1.0 - cosf(omega);
		b2 = b0;
		break;
	case 3:	//	BPF
		a0 = 1.0 + alpha;
		a1 = -2.0 * cosf(omega);
		a2 = 1.0 - alpha;
		b0 = q * alpha;
		b1 = 0;
		b2 = -1.0 * b0;
		break;
	default:
		a0 = 1 + alpha;
		a1 = -2 * cosf(omega);
		a2 = 1 - alpha;
		b0 = 1 - alpha;
		b1 = -2 * cosf(omega);
		b2 = 1 + alpha;
		break;
	}
	in_1 = (t > 2) ? input[t - 1] : 0.f;
	in_2 = (t > 2) ? input[t - 2] : 0.f;
	out_1 = (t > 2) ? output[t - 1] : 0.f;
	out_2 = (t > 2) ? output[t - 2] : 0.f;
	in_0 = (t > 2) ? input[t] : 0.f;

	temp = b0 / a0 * in_0+ b1 / a0 * in_1 + b2 / a0 * in_2
		- a1 / a0 * out_1 - a2 / a0 * out_2;

	output[t] =temp;

	return 0;
}

//	Saw波形の生成

float snd_saw(float t, float m) {
float g = t * m;
return (g - (float)snd_round(g)) * 2;
}

/*
float snd_sin(float t, float m) {
return msinf( (t * m) * PI_M * 2);
}


float snd_sqr(float t, float m) {
float g = snd_saw(t, m);
return (g > 0.25) ? 1 : -1;
}

*/


#endif