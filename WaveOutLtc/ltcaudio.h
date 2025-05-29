/*
MIT License

Copyright (c) [2016] [Hakan Soyalp]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#ifndef _LTCAUDIO_H_
#define _LTCAUDIO_H_

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <time.h>
#include "ltc.h"
#include <math.h> // ����math.hͷ�ļ���ʹ��sin����
#include <sys/stat.h>

// ������Ƶ����
#define NUM_CHANNELS 1
//#define SAMPLE_RATE 48000 
#define SAMPLE_RATE 48000 
//#define FPS 30   



FILE* fp;
// 
//typedef struct { 
//    float* recordedSamples;  // ���ڴ洢¼�Ƶ���Ƶ����
//    int maxFrameIndex;       // �����Ƶ֡��
//    int frameIndex;          // ��ǰ��Ƶ֡����
//    double currentTimestamp; // ��ǰ��Ƶ֡��ʱ���
//    int nSamplesPerSec;      // ÿ��Ĳ�����
//    int nChannels;           // ������
//} paData;
// ¼���߳����ݽṹ
typedef struct {
    float* recordedSamples;
    int maxFrameIndex;
    int frameIndex;
} paData;
// ����ص���������
//typedef void (*XojoCallback)(const char* message);
typedef void (* XojoCallback)(int);


//typedef void(__stdcall* XojoCallback)(const char*);

// ȫ�ֻص�����ָ�� 
static XojoCallback g_callback = NULL;
static CRITICAL_SECTION g_callbackLock;


static CRITICAL_SECTION waveCriticalSection;
static CRITICAL_SECTION stopltcthread;
static volatile int waveFreeBlockCount;
void CALLBACK waveOutProc(HWAVEOUT hWaveOut, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2);
void LogClose();
__declspec(dllexport) void  __stdcall  Myltc(ltcsnd_sample_t* sound, int bufferSize);
__declspec(dllexport) void   __stdcall  setCallback(XojoCallback callback);
//__declspec(dllexport) DWORD  __stdcall  startRecording(paData* data);
__declspec(dllexport) DWORD  __stdcall  startRecording(int data);
__declspec(dllexport) void   __stdcall  stopRecording();

__declspec(dllexport) int __stdcall LTC_Init(const char* startTime, const char* endTime, const char* fps);

__declspec(dllexport) int __stdcall LTC_Start();
__declspec(dllexport) int __stdcall LTC_Stop();
__declspec(dllexport) int __stdcall LTC_SetStopTime(const char* stopTime); // �ɶ�̬����
// ״̬��ȡ
__declspec(dllexport) int __stdcall LTC_GetCurrentTime(char* buffer); // ���Ϊ"HH:MM:SS:FF"
// ��Դ�ͷ�
__declspec(dllexport) void __stdcall LTC_Cleanup();
static void LogMessage(const char* format, ...);
static CRITICAL_SECTION g_stateLock;
#include <time.h>  // ��Ҫ����time.hͷ�ļ�
#define _WIN32_WINNT 0x0500 // ����ߵİ汾��

#endif  //_LTCAUDIO_H_
