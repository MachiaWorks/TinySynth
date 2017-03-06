#include <windows.h>
#include <mmsystem.h>
#include <math.h>

#include "synth.h"

/*
	■基本的な考え
	shortの範囲をMAXの値とする（WAVフォーマットの値の上限、shortに依存）
	すなわち、WAVを書き込む場合、-32768～+32767の範囲であることを確認して書き込む。
	上記範囲を超える場合、値の保証はできないため、スピーカが破損する恐れあり。

	■一番簡単な対策
	WAVデータの書き込みを行う際、shortの値以内に収めるようにする。
	ただこれでWAVのデータ型の範囲を超えた音が出る場合、音割れが起こるため注意。

	可能ならばデータ上限用にリミッタ（コンプレッサ）を実装するのが安全と思料。
*/

#define PI_M 3.14159265358979323846
#define SYNTH_WAV_MAX	256	//楽器の音量
#define MASTER_WAV_MAX	126	//マスターとなるミキサーの音量（と考えてもらえれば）
#define SYNTH_WAV_HZ	44100
#define SYNTH_WAV_HZ_INV	1/44100
#define SEQ_PTN_START	0
#define CHANNELS	2

#define pm  0.089f	//譜面1カウントの長さ
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
//	譜面1個のWAVのデータ格納量
static unsigned long ptime;

//----------------------------------------------------------------
// エントリポイント
//----------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	const unsigned char mid_seq[][MID_NOTE_MAX] = {
		//	メロディライン
		{0x00,k5g,k6c,k5b,k5g,k5g, k6c,k5b,k5g,k5e, },
		//	BD
		{0x00,k2e,k2e,k2e,k2e,k2e,k2e,k2e,k2e,k2e,},
	};

	const unsigned char mid_length[][MID_NOTE_MAX] = {
		//	メロディライン
		{16, 8,8,8,8,8,8,8,8,8,},
		//	BD
		 {16,8,8,8,8,8,8,8,4,4,},
	};
	//	各パートのボリューム
	const unsigned long volume[PLAY_PART_NO]
		= { SYNTH_WAV_MAX-40,SYNTH_WAV_MAX-60,};

	//	音色（というよりまずはオシレータの種類）
	//	0:mute。
	//	1:サイン
	//	2:矩形
	//	3:ノコギリ
	//	4:使っちゃいけない気がする音
	//	5:ノイズ
	//	6:キックドラム
	//	7:ノコギリ（重ね）
	//	8:三角波
	unsigned int Tone[PLAY_PART_NO] = { 2,6,};

	//	エンベロープ
	float envelope[][5] = {
		//	アタックレート、アタックレベル、ディケイレート、サステインレート、サステインタイム
		//	サステインレート：フェードアウトする時間。これを長く取ってサステインと同等の機能を実装

		//	サステインが0.0001以下だとエラーを起こす可能性あり。
		{ 0.06f, 1.f, 1.5f, 9.0f, 0.5f },//sin wave
		{ 0.045f, 1.5f, 0.15f, 1.0f, 0.0f } ,//bd
	};

	//	フィルタータイプの選択。
	//	1:LPF、2:HPF、3:BPF
	const unsigned char filter_type[PLAY_PART_NO] = { 2, 1, };

	float filter_param[][2] = {
		//	キーポイントにする周波数、Q（広がり）
		//	スペアナ見ながら調整すること
		{ 4000.0f, 0.40, }, //sin wave
		{ 150.0f, 1.0, },//bd
	};

	//	＋が左、-が右側とする
	const char pan[PLAY_PART_NO] = { 0,0,};

	const float pitch_info[][4] = {
		//	デチューンの値、ピッチシフトの値、LFOの倍率、LFOの周期
		{0.001f,0.0f,0.0f,0.0f, },
		{0.000f,0.0038f,0.0f,0.0f, },//bd
	};

	/*
	変数の初期化。
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

	//	仮決めの数値です
	unsigned long musictime = (unsigned long)(16.f*64.f* pm* SYNTH_WAV_HZ);//1[sec] = 44100[sample]

	//	最終的なアウトプット。
	float* out = (float*)GlobalAlloc(GPTR, sizeof(float)*musictime);	//	バッファの確保。
	
	//	float型で持つ、波形の生データ
	float* tone_data = (float*)GlobalAlloc(GPTR, sizeof(float)*musictime);

	//	最終的に持つデータ。
	short* wave_data = (short*)GlobalAlloc(GPTR, sizeof(short)*CHANNELS*musictime);	//チャンネル数を反映


	unsigned int i;
		//	1曲全体について処理を行う。
		for (i = 0; i < PLAY_PART_NO; i++) {
			seq_data = 0;	//	シーケンスの書き込み状況
			seq_ptr = SEQ_PTN_START;
			omega = 0.f;
			osc = 0.f;
			test_x = 0.f;
			//*out = { 0 };
			adsr_timer = 0;
			uc_state = ATTACK;

			for (unsigned long t = 0; t < musictime; t++) {
				seq_data++;
				//	バッファを初期化した。
				osc = 0.f;
				//	1音色のフレーズ格納するバッファをオフにした
				t2 = CHANNELS*t;
				tone_data[t] = (short)0;
				static float temp_x = 0.f;

				//	譜面読み込み。
				ptime = (mid_length[i][seq_ptr] * (long)(SYNTH_WAV_HZ*pm));/// speed[ptn_ptr]

				if (mid_seq[i][seq_ptr] != 0x00) {
					frq = mGetFrq(mid_seq[i][seq_ptr]);//周波数を得る

					//	音色の分岐。
					float phase = 0.0;

					switch (Tone[i]) {
					case 0:
						//	音なし
						omega = 0;
						osc = 0;
						break;

					case 1:
						//	のこぎり波
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
						//	矩形。
						omega = (FLOAT)(2.0f*PI_M * frq * (seq_data)* SYNTH_WAV_HZ_INV + test_x);
						osc = sinf(omega) + sinf(omega * 3) / 3 + sinf(omega * 5) / 5 + sinf(omega * 7) / 7 + sinf(omega * 9) / 9;
						break;

					case 3:
						//	ノコギリ波その２？
						omega = frq*t / SYNTH_WAV_HZ;
						omega -= snd_round(omega);
						osc = omega;
						break;
					case 5:
						//	きれいなノイズ。
						osc = f_rand();
						break;
					case 6:
						//	サイン波
						omega = (FLOAT)(2.0f*PI_M * frq * (seq_data)* SYNTH_WAV_HZ_INV + test_x);
						osc = sinf(omega);
						break;
					case 7:
						//	SuperSawを無理やり作りました
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
					case 8:	//	三角波
						omega = (FLOAT)(2.0f*PI_M * frq * (seq_data)* SYNTH_WAV_HZ_INV + test_x);
						for (int k = 1; k < 9; k += 2) {
							//1.0 / i / i * sin(M_PI * i / 2.0) * sin(2.0 * M_PI * i * f0 * n / pcm.fs);
							osc += 1.f / k / k*sinf(PI_M*k / 2.0f) * sinf(omega);
						}
						break;
					case 9:
						//	サイン波。
						omega = (FLOAT)(2.0f*PI_M * frq * (seq_data)* SYNTH_WAV_HZ_INV + test_x);
						osc = sinf(omega);
						break;


					default:
						//	音なし
						omega = 0;
						osc = 0;
						break;
					}
					wav_max = volume[i];
					/*
					
					
				}
				else {
					//	処理時間の省略処理
					omega = 0;
					osc = 0;
				}
				*/
				//	Amp処理
					
					//	エンベロープの状態分岐
					uc_state
					= (seq_data < (envelope[i][ENVELOPE_ATTACK_RATE] * SYNTH_WAV_HZ)) ? ATTACK
						: (seq_data <= (envelope[i][ENVELOPE_ATTACK_RATE] + envelope[i][ENVELOPE_DECAY_RATE])*SYNTH_WAV_HZ) ? DECAY
						: SUSTAIN;

					//	エンベロープの処理
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
						//	計算がおかしい。
						//	Sustainの値が微妙な実装。おそらくバッファを正確に埋められてない。
						//	ノイズは除去したがリファクタリングが必要。
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

					//	譜面が切り替わるときのダッキング。
					short fade_in = 75;
					temp_x = (seq_data < fade_in) ? temp_x - (temp_x / fade_in)*(fade_in - seq_data) : temp_x;

					short fade_out = 70;
					temp_x = (ptime - seq_data < fade_out) ? temp_x - (temp_x / fade_out)*(fade_out - (ptime - seq_data)) : temp_x;

					//	チャンネルごとにデータを書き込む。
					//	各chに音を入力する。
					tone_data[t] = temp_x;


					//	VCF処理。
					Filter(tone_data, tone_data, t, filter_type[i], filter_param[i][0], filter_param[i][1]);

				}
				else {
					//	強制的にゼロに収束させてる
					tone_data[t] = 0;
				}

				//	エフェクト用にポインタを参照する形とする。
				in = tone_data;

				//	ここにエフェクトを挟めるようにしておく。
				out[t] = in[t];

				//	計算結果をバッファコピーする。
				in[t] = out[t];

				for (int l = 0; l < CHANNELS; l++) {
					short pan_result = pan[i];

					wave_data[t2 + l] = (pan_result<0 && l==1 )
						                 ? wave_data[t2+l] + (wav_max - pan_result)*out[t]	//右側からマイナス
						                :(pan_result>0 && l==0 )
						                 ? wave_data[t2+l] + (wav_max - pan_result )*out[t]		//	左からマイナス
					                    :wave_data[t2+l] + wav_max*out[t];
																													//wave_data[t2 + l] = wave_data[t2 + l] + wav_max*out[t] * total_wav;

					//	クリッピング防止処理。
					wave_data[t2 + l] = (wave_data[t2 + l] < -32767.0f) ? -32767.0f : wave_data[t2 + l];	//	クリッピング防止
					wave_data[t2 + l] = (wave_data[t2 + l] > 32767.0f) ? 32767.0f : wave_data[t2 + l];	//	クリッピング防止
				}


				//	シーケンスデータの更新。
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
		}	//データ書き込み処理終わり
	//}
		//	最終的なエフェクト。
		//	リバーブとリミッタを予定。
		for (int t = 0; t < musictime * CHANNELS; t++) {
			//	リミティング。
			wave_data[t] *= MASTER_WAV_MAX;
			
			wave_data[t] = (wave_data[t] < -32767.0f) ? -32767.0f : wave_data[t];	//	クリッピング防止
			wave_data[t] = (wave_data[t] > 32767.0f) ? 32767.0f : wave_data[t];	//	クリッピング防止
		}




		//WAVEデバイス設定
	WAVEFORMATEX wf;                              //WAVEFORMATEX 構造体
	wf.wFormatTag = WAVE_FORMAT_PCM;                //これはこのまま
	wf.nChannels = CHANNELS;                               //モノラル ステレオなら'2'。データ量が2倍なのでそのままだとピッチ上がる
	wf.nSamplesPerSec = SYNTH_WAV_HZ;                      //44100Hz
	wf.wBitsPerSample = 16;                         //16ビット
	wf.nBlockAlign = wf.nChannels*wf.wBitsPerSample / 8;       //計算
	wf.nAvgBytesPerSec = wf.nSamplesPerSec*wf.nBlockAlign;   //計算
	wf.cbSize = 0;                                           //計算
	HWAVEOUT hWOut;
	waveOutOpen(&hWOut, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);

	//WAVE情報設定
	WAVEHDR wh;
	wh.lpData = (LPSTR)wave_data;	//	作成したデータを書き込む
	wh.dwBufferLength = sizeof(short)*CHANNELS*musictime;//<--------再生音楽時間（変え忘れないように！）
	wh.dwFlags = 0;
	wh.dwLoops = 1;//1回だけ再生
	wh.dwBytesRecorded = 0;
	wh.dwUser = 0;
	wh.lpNext = NULL;
	wh.reserved = 0;

	//	再生
	waveOutPrepareHeader(hWOut, &wh, sizeof(WAVEHDR));
	waveOutWrite(hWOut, &wh, sizeof(WAVEHDR));


	MessageBox(NULL, "by MachiaWorx \n2017/03/07", "synthesizer test", MB_OK);


	PostQuitMessage(0);
	ExitProcess(0);//ちゃんとクローズさせてもいいですが、こいつで一発！
}