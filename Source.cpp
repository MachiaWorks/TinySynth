#include <windows.h>
#include <mmsystem.h>
#include <math.h>

#include "synth.h"

/*
	����{�I�ȍl��
	short�͈̔͂�MAX�̒l�Ƃ���iWAV�t�H�[�}�b�g�̒l�̏���Ashort�Ɉˑ��j
	���Ȃ킿�AWAV���������ޏꍇ�A-32768�`+32767�͈̔͂ł��邱�Ƃ��m�F���ď������ށB
	��L�͈͂𒴂���ꍇ�A�l�̕ۏ؂͂ł��Ȃ����߁A�X�s�[�J���j�����鋰�ꂠ��B

	����ԊȒP�ȑ΍�
	WAV�f�[�^�̏������݂��s���ہAshort�̒l�ȓ��Ɏ��߂�悤�ɂ���B
	���������WAV�̃f�[�^�^�͈̔͂𒴂��������o��ꍇ�A�����ꂪ�N���邽�ߒ��ӁB

	�\�Ȃ�΃f�[�^����p�Ƀ��~�b�^�i�R���v���b�T�j����������̂����S�Ǝv���B
*/

#define PI_M 3.14159265358979323846
#define SYNTH_WAV_MAX	256	//�y��̉���
#define MASTER_WAV_MAX	126	//�}�X�^�[�ƂȂ�~�L�T�[�̉��ʁi�ƍl���Ă��炦��΁j
#define SYNTH_WAV_HZ	44100
#define SYNTH_WAV_HZ_INV	1/44100
#define SEQ_PTN_START	0
#define CHANNELS	2

#define pm  0.089f	//����1�J�E���g�̒���
#define PLAY_PART_NO	2
#define MID_NOTE_MAX 10

#define ENVELOPE_ATTACK_RATE	0
#define ENVELOPE_ATTACK_LEVEL	1
#define ENVELOPE_DECAY_RATE		2
#define ENVELOPE_SUSTAIN_RATE	3
#define ENVELOPE_SUSTAIN_LEVEL	4

enum
{
	ATTACK=0,
	DECAY,
	SUSTAIN,
};

static float frq = 0.f;
static short pre_wav = 0;
//	����1��WAV�̃f�[�^�i�[��
static unsigned long ptime;

//----------------------------------------------------------------
// �G���g���|�C���g
//----------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	const unsigned char mid_seq[][MID_NOTE_MAX] = {
		//	�����f�B���C��
		{0x00,k5g,k6c,k5b,k5g,k5g, k6c,k5b,k5g,k5e, },
		//	BD
		{0x00,k2e,k2e,k2e,k2e,k2e,k2e,k2e,k2e,k2e,},
	};

	const unsigned char mid_length[][MID_NOTE_MAX] = {
		//	�����f�B���C��
		{16, 8,8,8,8,8,8,8,8,8,},
		//	BD
		 {16,8,8,8,8,8,8,8,4,4,},
	};
	//	�e�p�[�g�̃{�����[��
	const unsigned long volume[PLAY_PART_NO]
		= { SYNTH_WAV_MAX-40,SYNTH_WAV_MAX-60,};

	//	���F�i�Ƃ������܂��̓I�V���[�^�̎�ށj
	//	0:mute�B
	//	1:�T�C��
	//	2:��`
	//	3:�m�R�M��
	//	4:�g�����Ⴂ���Ȃ��C�����鉹
	//	5:�m�C�Y
	//	6:�L�b�N�h����
	//	7:�m�R�M���i�d�ˁj
	//	8:�O�p�g
	unsigned int Tone[PLAY_PART_NO] = { 2,6,};

	//	�G���x���[�v
	float envelope[][5] = {
		//	�A�^�b�N���[�g�A�A�^�b�N���x���A�f�B�P�C���[�g�A�T�X�e�C�����[�g�A�T�X�e�C���^�C��
		//	�T�X�e�C�����[�g�F�t�F�[�h�A�E�g���鎞�ԁB����𒷂�����ăT�X�e�C���Ɠ����̋@�\������

		//	�T�X�e�C����0.0001�ȉ����ƃG���[���N�����\������B
		{ 0.06f, 1.f, 1.5f, 9.0f, 0.5f },//sin wave
		{ 0.045f, 1.5f, 0.15f, 1.0f, 0.0f } ,//bd
	};

	//	�t�B���^�[�^�C�v�̑I���B
	//	1:LPF�A2:HPF�A3:BPF
	const unsigned char filter_type[PLAY_PART_NO] = { 2, 1, };

	float filter_param[][2] = {
		//	�L�[�|�C���g�ɂ�����g���AQ�i�L����j
		//	�X�y�A�i���Ȃ��璲�����邱��
		{ 4000.0f, 0.40, }, //sin wave
		{ 150.0f, 1.0, },//bd
	};

	//	�{�����A-���E���Ƃ���
	const char pan[PLAY_PART_NO] = { 0,0,};

	const float pitch_info[][4] = {
		//	�f�`���[���̒l�A�s�b�`�V�t�g�̒l�ALFO�̔{���ALFO�̎���
		{0.001f,0.0f,0.0f,0.0f, },
		{0.000f,0.0038f,0.0f,0.0f, },//bd
	};

	/*
	�ϐ��̏������B
	*/
	unsigned short wav_max;
	static float osc;
	float omega = 0.f;
	int seq_data;
	static unsigned char seq_ptr;
	float test_x = 0.f;
	long adsr_timer = 0;
	unsigned char uc_state = 0;
	unsigned int t2;
	float* in;

	//	�����߂̐��l�ł�
	unsigned long musictime = (unsigned long)(16.f*64.f* pm* SYNTH_WAV_HZ);//1[sec] = 44100[sample]

	//	�ŏI�I�ȃA�E�g�v�b�g�B
	float* out = (float*)GlobalAlloc(GPTR, sizeof(float)*musictime);	//	�o�b�t�@�̊m�ہB
	
	//	float�^�Ŏ��A�g�`�̐��f�[�^
	float* tone_data = (float*)GlobalAlloc(GPTR, sizeof(float)*musictime);

	//	�ŏI�I�Ɏ��f�[�^�B
	short* wave_data = (short*)GlobalAlloc(GPTR, sizeof(short)*CHANNELS*musictime);	//�`�����l�����𔽉f


	unsigned int i;
		//	1�ȑS�̂ɂ��ď������s���B
		for (i = 0; i < PLAY_PART_NO; i++) {
			seq_data = 0;	//	�V�[�P���X�̏������ݏ�
			seq_ptr = SEQ_PTN_START;
			omega = 0.f;
			osc = 0.f;
			test_x = 0.f;
			//*out = { 0 };
			adsr_timer = 0;
			uc_state = ATTACK;

			for (unsigned long t = 0; t < musictime; t++) {
				seq_data++;
				//	�o�b�t�@�������������B
				osc = 0.f;
				//	1���F�̃t���[�Y�i�[����o�b�t�@���I�t�ɂ���
				t2 = CHANNELS*t;
				tone_data[t] = (short)0;
				static float temp_x = 0.f;

				//	���ʓǂݍ��݁B
				ptime = (mid_length[i][seq_ptr] * (long)(SYNTH_WAV_HZ*pm));/// speed[ptn_ptr]

				if (mid_seq[i][seq_ptr] != 0x00) {
					frq = mGetFrq(mid_seq[i][seq_ptr]);//���g���𓾂�

					//	���F�̕���B
					float phase = 0.0;

					switch (Tone[i]) {
					case 0:
						//	���Ȃ�
						omega = 0;
						osc = 0;
						break;

					case 1:
						//	�̂�����g
						/*

						*/
						omega = (FLOAT)(2.0f*PI_M * frq * (seq_data)* SYNTH_WAV_HZ_INV + test_x);
						for (int k = 1; k < 10; k++)
						{
							osc += sinf(omega * k * 2) / k / 2;
						}
						osc /= 2.5;
						break;

					case 2:
						//	��`�B
						omega = (FLOAT)(2.0f*PI_M * frq * (seq_data)* SYNTH_WAV_HZ_INV + test_x);
						osc = sinf(omega) + sinf(omega * 3) / 3 + sinf(omega * 5) / 5 + sinf(omega * 7) / 7 + sinf(omega * 9) / 9;
						break;

					case 3:
						//	�m�R�M���g���̂Q�H
						omega = frq*t / SYNTH_WAV_HZ;
						omega -= snd_round(omega);
						osc = omega;
						break;
					case 5:
						//	���ꂢ�ȃm�C�Y�B
						osc = f_rand();
						break;
					case 6:
						//	�T�C���g
						omega = (FLOAT)(2.0f*PI_M * frq * (seq_data)* SYNTH_WAV_HZ_INV + test_x);
						osc = sinf(omega);
						break;
					case 7:
						//	SuperSaw�𖳗������܂���
						omega = (FLOAT)(2.0f*PI_M * frq * (seq_data)* SYNTH_WAV_HZ_INV + test_x);
						for (int k = 1; k < 10; k++)
						{
							osc += sinf(omega * k * 2) / k / 2;
						}
						osc /= 2.5;
						omega /= 2.f;
						for (int k = 1; k < 10; k++)
						{
							osc += sinf(omega * k * 2) / k / 2;
						}
						osc /= 2.5;

						break;
					case 8:	//	�O�p�g
						omega = (FLOAT)(2.0f*PI_M * frq * (seq_data)* SYNTH_WAV_HZ_INV + test_x);
						for (int k = 1; k < 9; k += 2) {
							//1.0 / i / i * sin(M_PI * i / 2.0) * sin(2.0 * M_PI * i * f0 * n / pcm.fs);
							osc += 1.f / k / k*sinf(PI_M*k / 2.0f) * sinf(omega);
						}
						break;
					case 9:
						//	�T�C���g�B
						omega = (FLOAT)(2.0f*PI_M * frq * (seq_data)* SYNTH_WAV_HZ_INV + test_x);
						osc = sinf(omega);
						break;


					default:
						//	���Ȃ�
						omega = 0;
						osc = 0;
						break;
					}
					wav_max = volume[i];
					/*
					
					
				}
				else {
					//	�������Ԃ̏ȗ�����
					omega = 0;
					osc = 0;
				}
				*/
				//	Amp����
					
					//	�G���x���[�v�̏�ԕ���
					uc_state
					= (seq_data < (envelope[i][ENVELOPE_ATTACK_RATE] * SYNTH_WAV_HZ)) ? ATTACK
						: (seq_data <= (envelope[i][ENVELOPE_ATTACK_RATE] + envelope[i][ENVELOPE_DECAY_RATE])*SYNTH_WAV_HZ) ? DECAY
						: SUSTAIN;

					//	�G���x���[�v�̏���
					switch (uc_state) {
					case ATTACK:
						temp_x = osc * (envelope[i][ENVELOPE_ATTACK_LEVEL]
							/ (envelope[i][ENVELOPE_ATTACK_RATE] * SYNTH_WAV_HZ))
							* seq_data;
						break;

					case DECAY:
						temp_x = osc * (envelope[i][ENVELOPE_ATTACK_LEVEL]
							- ((envelope[i][ENVELOPE_ATTACK_LEVEL] - envelope[i][ENVELOPE_SUSTAIN_LEVEL])
							/ (envelope[i][ENVELOPE_DECAY_RATE] * SYNTH_WAV_HZ)
							* ((seq_data) - envelope[i][ENVELOPE_ATTACK_RATE] * SYNTH_WAV_HZ)));
						break;
						//		osc * ( 1.0-( ( 1.0-0.5) / (1.0*44100)*(seq_data-1*44100) ) ) )

					case SUSTAIN:
						//	�v�Z�����������B
						//	Sustain�̒l�������Ȏ����B�����炭�o�b�t�@�𐳊m�ɖ��߂��ĂȂ��B
						//	�m�C�Y�͏������������t�@�N�^�����O���K�v�B
						temp_x = osc *(envelope[i][ENVELOPE_SUSTAIN_LEVEL]
							- (envelope[i][ENVELOPE_SUSTAIN_LEVEL]
							/ (envelope[i][ENVELOPE_SUSTAIN_RATE] *SYNTH_WAV_HZ
							* ((seq_data+1) - (envelope[i][ENVELOPE_ATTACK_RATE] + envelope[i][ENVELOPE_DECAY_RATE])*SYNTH_WAV_HZ))));
						//temp_x = osc * sustainLevel;
						//		osc * ( 0.5 - 0.5/(99-1-1)*44100 ) * ( seq_data - (1+1)*44100 )
						break;

					default:
						temp_x = osc * envelope[i][ENVELOPE_SUSTAIN_LEVEL];
						break;
					}

					//	���ʂ��؂�ւ��Ƃ��̃_�b�L���O�B
					short fade_in = 75;
					temp_x = (seq_data < fade_in) ? temp_x - (temp_x / fade_in)*(fade_in - seq_data) : temp_x;

					short fade_out = 70;
					temp_x = (ptime - seq_data < fade_out) ? temp_x - (temp_x / fade_out)*(fade_out - (ptime - seq_data)) : temp_x;

					//	�`�����l�����ƂɃf�[�^���������ށB
					//	�ech�ɉ�����͂���B
					tone_data[t] = temp_x;


					//	VCF�����B
					Filter(tone_data, tone_data, t, filter_type[i], filter_param[i][0], filter_param[i][1]);

				}
				else {
					//	�����I�Ƀ[���Ɏ��������Ă�
					tone_data[t] = 0;
				}

				//	�G�t�F�N�g�p�Ƀ|�C���^���Q�Ƃ���`�Ƃ���B
				in = tone_data;

				//	�����ɃG�t�F�N�g�����߂�悤�ɂ��Ă����B
				out[t] = in[t];

				//	�v�Z���ʂ��o�b�t�@�R�s�[����B
				in[t] = out[t];

				for (int l = 0; l < CHANNELS; l++) {
					short pan_result = pan[i];

					wave_data[t2 + l] = (pan_result<0 && l==1 )
						                 ? wave_data[t2+l] + (wav_max - pan_result)*out[t]	//�E������}�C�i�X
						                :(pan_result>0 && l==0 )
						                 ? wave_data[t2+l] + (wav_max - pan_result )*out[t]		//	������}�C�i�X
					                    :wave_data[t2+l] + wav_max*out[t];
																													//wave_data[t2 + l] = wave_data[t2 + l] + wav_max*out[t] * total_wav;

					//	�N���b�s���O�h�~�����B
					wave_data[t2 + l] = (wave_data[t2 + l] < -32767.0f) ? -32767.0f : wave_data[t2 + l];	//	�N���b�s���O�h�~
					wave_data[t2 + l] = (wave_data[t2 + l] > 32767.0f) ? 32767.0f : wave_data[t2 + l];	//	�N���b�s���O�h�~
				}


				//	�V�[�P���X�f�[�^�̍X�V�B
				seq_ptr = (seq_data % ptime == 0) ? seq_ptr + 1 : seq_ptr;
				seq_data = (seq_data % ptime == 0) ? 0 : seq_data;

				test_x = (seq_data == 0) ? omega : test_x;
				test_x = (mid_seq[i][seq_ptr] == 0x00) ? 0.f : test_x;

				adsr_timer = (seq_data % ptime == 0) ? 0 : adsr_timer++;
				uc_state = (seq_data % ptime == 0) ? ATTACK : uc_state;

				if (seq_ptr >= MID_NOTE_MAX) {
					break;
				}

			}
		}	//�f�[�^�������ݏ����I���
	//}
		//	�ŏI�I�ȃG�t�F�N�g�B
		//	���o�[�u�ƃ��~�b�^��\��B
		for (int t = 0; t < musictime * CHANNELS; t++) {
			//	���~�e�B���O�B
			wave_data[t] *= MASTER_WAV_MAX;
			
			wave_data[t] = (wave_data[t] < -32767.0f) ? -32767.0f : wave_data[t];	//	�N���b�s���O�h�~
			wave_data[t] = (wave_data[t] > 32767.0f) ? 32767.0f : wave_data[t];	//	�N���b�s���O�h�~
		}




		//WAVE�f�o�C�X�ݒ�
	WAVEFORMATEX wf;                              //WAVEFORMATEX �\����
	wf.wFormatTag = WAVE_FORMAT_PCM;                //����͂��̂܂�
	wf.nChannels = CHANNELS;                               //���m���� �X�e���I�Ȃ�'2'�B�f�[�^�ʂ�2�{�Ȃ̂ł��̂܂܂��ƃs�b�`�オ��
	wf.nSamplesPerSec = SYNTH_WAV_HZ;                      //44100Hz
	wf.wBitsPerSample = 16;                         //16�r�b�g
	wf.nBlockAlign = wf.nChannels*wf.wBitsPerSample / 8;       //�v�Z
	wf.nAvgBytesPerSec = wf.nSamplesPerSec*wf.nBlockAlign;   //�v�Z
	wf.cbSize = 0;                                           //�v�Z
	HWAVEOUT hWOut;
	waveOutOpen(&hWOut, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);

	//WAVE���ݒ�
	WAVEHDR wh;
	wh.lpData = (LPSTR)wave_data;	//	�쐬�����f�[�^����������
	wh.dwBufferLength = sizeof(short)*CHANNELS*musictime;//<--------�Đ����y���ԁi�ς��Y��Ȃ��悤�ɁI�j
	wh.dwFlags = 0;
	wh.dwLoops = 1;//1�񂾂��Đ�
	wh.dwBytesRecorded = 0;
	wh.dwUser = 0;
	wh.lpNext = NULL;
	wh.reserved = 0;

	//	�Đ�
	waveOutPrepareHeader(hWOut, &wh, sizeof(WAVEHDR));
	waveOutWrite(hWOut, &wh, sizeof(WAVEHDR));


	MessageBox(NULL, "by MachiaWorx \n2017/03/07", "synthesizer test", MB_OK);


	PostQuitMessage(0);
	ExitProcess(0);//�����ƃN���[�Y�����Ă������ł����A�����ňꔭ�I
}