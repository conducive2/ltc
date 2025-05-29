

#include "ltcaudio.h"  


static SMPTETimecode prevStime = { 0 };
static BOOL isFlastRun = TRUE;
static HANDLE g_hRecordingThread = NULL; 
static HANDLE g_hRecordingEvent = NULL; 
static HANDLE hThread;
static int lastSentValue = -1;
static volatile BOOL g_bIsRecording = FALSE;
HWAVEIN hWaveIn;
WAVEHDR waveHeader;
static HANDLE g_hDataReadyEvent = NULL;  // ���ݾ����¼�
static HANDLE g_hWorkerThread = NULL;   // �����߳̾��
static volatile BOOL g_bWorkerRunning = FALSE;  // �����߳����б�־

paData* data;
#define WAVEIN_STOP_TIMEOUT 1000
static double FPS = 30;
// ȫ�ֱ���
static int g_currentValue = -1;  // �洢��ǰֵ
static CRITICAL_SECTION g_valueLock;  // �߳���

static int g_currentTimecode = -1;  // �洢��ǰʱ����ֵ
static CRITICAL_SECTION g_timecodeLock;  // �߳���

// ��ʼ����
void InitializeLocks() {
    InitializeCriticalSection(&g_timecodeLock);
}

// ������
void CleanupLocks() {
    DeleteCriticalSection(&g_timecodeLock);
}

// ���õ�ǰʱ����ֵ
void SetCurrentTimecode(int value) {
    EnterCriticalSection(&g_timecodeLock);
    g_currentTimecode = value;
    LeaveCriticalSection(&g_timecodeLock);
}

// ��ȡ��ǰʱ����ֵ
__declspec(dllexport) int __stdcall GetCurrentTimecode() {
    EnterCriticalSection(&g_timecodeLock);
    int value = g_currentTimecode;
    LeaveCriticalSection(&g_timecodeLock);
    return value;
}




//BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
//    if (fdwReason == DLL_PROCESS_ATTACH) {
//        InitializeCriticalSection(&g_callbackLock);
//    }
//    else if (fdwReason == DLL_PROCESS_DETACH) {
//        DeleteCriticalSection(&g_callbackLock);
//    }
//    return TRUE;
//}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        InitializeLocks();
        InitializeCriticalSection(&g_stateLock); 
    }
    else if (fdwReason == DLL_PROCESS_DETACH) {
        CleanupLocks();
        DeleteCriticalSection(&g_stateLock);
    }
    return TRUE;
}



__declspec(dllexport) void __stdcall setCallback(XojoCallback callback) {
    EnterCriticalSection(&g_callbackLock);
    g_callback = callback;
    LeaveCriticalSection(&g_callbackLock);
}

void printMessage(int value) {
    if (value == lastSentValue) return  ;

    EnterCriticalSection(&g_callbackLock);
    if (g_callback) {
        g_callback(value);
        lastSentValue = value; 
    }
    LeaveCriticalSection(&g_callbackLock);
}



#define MAX_FRAME_DEVIATION 5   
#define MIN_CONSECUTIVE_FRAMES 3  
int isFirstRun = 1;   
int consecutiveFrames = 0;   

unsigned long timecode_to_frames(const SMPTETimecode* tc) {
    return tc->hours * 3600 * FPS +
        tc->mins * 60 * FPS +
        tc->secs * FPS +
        tc->frame;
}
// ������֮֡���ʵ��֡����ǽ��ƣ�
int calculateFrameDeviation(const SMPTETimecode* current, const SMPTETimecode* previous) {
    unsigned long current_frames = timecode_to_frames(current);
    if (current_frames < 10) return 3;
    unsigned long previous_frames = timecode_to_frames(previous);
    return abs((int)(current_frames - previous_frames));
}
int  int_timecode_to_frames(const SMPTETimecode* tc) {
    return tc->hours * 3600 * FPS +
        tc->mins * 60 * FPS +
        tc->secs * FPS +
        tc->frame;
}


__declspec(dllexport) void __stdcall Myltc(ltcsnd_sample_t* sound, int bufferSize) {
    //LTCDecoder* decoder = ltc_decoder_create(3200, 8);
    LTCDecoder* decoder = ltc_decoder_create(2400, 8);
    if (decoder == NULL) {
        return;
    }

    LTCFrameExt frame;
    ltc_decoder_write(decoder, sound, bufferSize, 0);
    while (ltc_decoder_read(decoder, &frame)) {
        SMPTETimecode stime;
        ltc_frame_to_time(&stime, &frame.ltc, 0);
        if (isFirstRun) {
            prevStime = stime;
            isFirstRun = 0;
            consecutiveFrames = 3;
        }
        int frameDeviation = calculateFrameDeviation(&stime, &prevStime);

        if (frameDeviation > MAX_FRAME_DEVIATION) {
            if (consecutiveFrames >= MIN_CONSECUTIVE_FRAMES) {
                char buffer[100];
                snprintf(buffer, sizeof(buffer), "%02d-%02d-%02d%c%02d\n", stime.hours,
                    stime.mins, stime.secs, '-', stime.frame);
            }
            consecutiveFrames = 0;
        }
        else {
            consecutiveFrames++;
        }
        if (consecutiveFrames >= MIN_CONSECUTIVE_FRAMES) {
            char buffer[100];
            snprintf(buffer, sizeof(buffer), "%02d-%02d-%02d%c%02d\n", stime.hours,
                stime.mins, stime.secs, '-', stime.frame);
            fp = fopen("time_info11.txt", "a");
            fprintf(fp, "time %s", buffer);
            fclose(fp);

            // �洢ʱ����ֵ��ȫ�ֱ���
            SetCurrentTimecode(int_timecode_to_frames(&stime));
        }
        prevStime = stime;
    }

    ltc_decoder_free(decoder);
}


void CALLBACK waveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
 
    if (uMsg == WIM_CLOSE || waveInGetNumDevs() == 0) {
        waveInReset(hwi);
        return;
    }

    if (uMsg == WIM_DATA && g_bIsRecording) {
        paData* data1 = (paData*)dwInstance;
        WAVEHDR* waveHeader = (WAVEHDR*)dwParam1;
        if (waveHeader->dwBytesRecorded > 0) {
            Myltc((ltcsnd_sample_t*)waveHeader->lpData, waveHeader->dwBytesRecorded / sizeof(ltcsnd_sample_t));
        }
        waveInAddBuffer(hwi, waveHeader, sizeof(WAVEHDR));
    }
}
 


// ¼���̣߳����¼�������ʽ������Ƶ����
DWORD WINAPI RecordingThread(LPVOID lpParameter)
{
    paData* data = (paData*)malloc(sizeof(paData));
    if (data == NULL) {
        printMessage("Failed to allocate memory for paData.\n");
        return 1;
    }
    g_bIsRecording = TRUE;
    int numSamples = SAMPLE_RATE / FPS * 2;
    //int numSamples = SAMPLE_RATE / FPS;
    data->maxFrameIndex = numSamples;
    data->frameIndex = 0;
    data->recordedSamples = (float*)malloc(numSamples * sizeof(float));
    if (data->recordedSamples == NULL) {
        printMessage("Failed to allocate memory for recording.\n");
        free(data);
        return 1;
    }

    // ������Ƶ�¼�����������ص�����
    HANDLE hAudioEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (hAudioEvent == NULL) {
        printMessage("Failed to create audio event.\n");
        free(data->recordedSamples);
        free(data);
        return 1;
    }

    MMRESULT result;
    WAVEFORMATEX waveFormat;
    waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat.nChannels = NUM_CHANNELS;
    waveFormat.nSamplesPerSec = SAMPLE_RATE;
    waveFormat.nAvgBytesPerSec = SAMPLE_RATE * NUM_CHANNELS;
    waveFormat.nBlockAlign = NUM_CHANNELS;
    waveFormat.wBitsPerSample = 8;
    waveFormat.cbSize = 0;

    // CALLBACK_EVENT ģʽ���� hAudioEvent ����
    result = waveInOpen(&hWaveIn, WAVE_MAPPER, &waveFormat, (DWORD_PTR)hAudioEvent, (DWORD_PTR)data, CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) {
        printMessage("Failed to open audio input device.\n");
        CloseHandle(hAudioEvent);
        free(data->recordedSamples);
        free(data);
        return 1;
    }

    // ׼����Ƶ������
    waveHeader.lpData = (LPSTR)malloc(numSamples);
    if (waveHeader.lpData == NULL) {
        printMessage("Failed to allocate memory for wave header.\n");
        waveInClose(hWaveIn);
        CloseHandle(hAudioEvent);
        free(data->recordedSamples);
        free(data);
        return 1;
    }
    waveHeader.dwBufferLength = numSamples;
    waveHeader.dwFlags = 0;
    waveHeader.dwLoops = 1;
    waveHeader.dwUser = 0;
    waveHeader.lpNext = NULL;

    waveInPrepareHeader(hWaveIn, &waveHeader, sizeof(WAVEHDR));
    waveInAddBuffer(hWaveIn, &waveHeader, sizeof(WAVEHDR));

    result = waveInStart(hWaveIn);
    if (result != MMSYSERR_NOERROR) {
        printMessage("Failed to start recording.\n");
        free(waveHeader.lpData);
        waveInClose(hWaveIn);
        CloseHandle(hAudioEvent);
        free(data->recordedSamples);
        free(data);
        return 1;
    }

    // �߳�ѭ�����ȴ���Ƶ�¼������ڶ����߳��д�����Ƶ����
    while (g_bIsRecording) {
        if (WaitForSingleObject(hAudioEvent, 100) == WAIT_OBJECT_0) {
            // ��黺�����Ƿ������¼��
            if (waveHeader.dwFlags & WHDR_DONE) {
                if (waveHeader.dwBytesRecorded > 0) {
                    Myltc((ltcsnd_sample_t*)waveHeader.lpData,
                        waveHeader.dwBytesRecorded / sizeof(ltcsnd_sample_t));
                }
                // ���¼��뻺�����Լ���¼��
                waveInAddBuffer(hWaveIn, &waveHeader, sizeof(WAVEHDR));
            }
        }
    }

    // ֹͣ¼������Դ����
    waveInReset(hWaveIn);
    waveInStop(hWaveIn);
    waveInUnprepareHeader(hWaveIn, &waveHeader, sizeof(WAVEHDR));
    waveInClose(hWaveIn);
    free(waveHeader.lpData);
    free(data->recordedSamples);
    free(data);
    CloseHandle(hAudioEvent);

    g_bIsRecording = FALSE;
    return 0;
}

 
__declspec(dllexport) DWORD __stdcall startRecording(int fps)
{ 
    FPS = fps; 
    if (g_hRecordingEvent != NULL) {
        CloseHandle(g_hRecordingEvent);
        g_hRecordingEvent = NULL;
    }
    if (g_hRecordingThread != NULL) {
        SetEvent(g_hRecordingEvent);
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(g_hRecordingEvent);
        g_hRecordingEvent = NULL;
        g_hRecordingThread = NULL;
    } 

    g_hRecordingEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_hRecordingThread = CreateThread(NULL, 0, RecordingThread, NULL, 0, NULL);
    if (g_hRecordingThread == NULL) {
        printMessage("Failed to create thread.");
        return 1;
    }
    hThread = g_hRecordingThread;
    return 0;
}


  BOOL IsAudioDeviceConnected() {
      UINT deviceCount = waveInGetNumDevs();
      return (deviceCount > 0);
  }

__declspec(dllexport) void   __stdcall  stopRecording() { 
 
    fp = fopen("time_info11.txt", "a");
    fprintf(fp, "1\n");
    fclose(fp);
    if (g_hRecordingEvent != NULL && g_hRecordingThread != NULL) {
        g_bIsRecording = FALSE; // ���ֹͣ
        SetEvent(g_hRecordingEvent);
        // �ȴ�¼���߳��˳�
        WaitForSingleObject(g_hRecordingThread, INFINITE);
        CloseHandle(g_hRecordingThread);  // �ر��߳̾��
        g_hRecordingThread = NULL;
        CloseHandle(g_hRecordingEvent);
        g_hRecordingEvent = NULL;
    
    }
}

#include "ltcaudio.h"
typedef struct {
	const char* StartValue;
	const char* EndValue;
	const char* MyFps;
} LTCTimeData;
typedef struct {
	LTCEncoder* encoder;
	CRITICAL_SECTION cs;
	HWAVEOUT hWaveOut;
	WAVEHDR** blocks;
	int stopTime[4];
	volatile int running;
	int waveFreeBlockCount;
	int blockCount;
	int framesPerBlock;
	//double fps; // �����ֶ�
	const char* fps;
	//const char* endTime; 
    char endTime[32];
    volatile int stopTimeUpdated; // ���±�� 
} LTC_State;
// �̲߳����ṹ��
typedef struct {
	LTC_State* state;
	HANDLE hThread;
	unsigned threadId;
} ThreadParams;
// ȫ���߳̾��
static ThreadParams g_thread = { 0 };
static unsigned __stdcall LTC_Thread(void* param);

static CRITICAL_SECTION logLock;
static FILE* logFile = NULL;
static BOOL initialized = FALSE;


static LTC_State* g_state = NULL;
LTC_State* GetState() {
    EnterCriticalSection(&g_stateLock);
    LTC_State* state = g_state;
    LeaveCriticalSection(&g_stateLock);
    return state;
}
void LogInit() {
    if (initialized) return;

    InitializeCriticalSection(&logLock);

    // �����ڴ�����־�ļ�
    time_t now = time(NULL);
    char filename[256];
    strftime(filename, sizeof(filename), "time_info_%Y-%m-%d.log", localtime(&now));

    logFile = fopen(filename, "a");
    atexit(LogClose);
    initialized = TRUE;
}

static void LogMessage(const char* format, ...) {
    //static CRITICAL_SECTION logLock;
    //static BOOL initialized = FALSE;

    //// ��ʼ���ٽ�����ִֻ��һ�Σ�
    //if (!initialized) {
    //    InitializeCriticalSection(&logLock);
    //    initialized = TRUE;
    //}

    //EnterCriticalSection(&logLock);

    //// ��ȡ��ǰʱ��
    //time_t now = time(NULL);
    //struct tm* tm_info = localtime(&now);
    //char timestamp[32];
    //strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    //// ����־�ļ���׷��ģʽ��
    //FILE* fp = fopen("time_info11.txt", "a");
    //if (fp) {
    //    // ��д��ʱ���
    //    fprintf(fp, "[%s] ", timestamp);

    //    // д���û��ṩ����־����
    //    va_list args;
    //    va_start(args, format);
    //    vfprintf(fp, format, args);
    //    va_end(args);

    //    // ȷ�����У������ʽ�ַ���û����\n��β��
    //    if (format[strlen(format) - 1] != '\n') {
    //        fputc('\n', fp);
    //    }

    //    fclose(fp);
    //}
    //else {
    //    // ����ļ���ʧ�ܣ������stderr������̨��
    //    fprintf(stderr, "[%s] ", timestamp);
    //    va_list args;
    //    va_start(args, format);
    //    vfprintf(stderr, format, args);
    //    va_end(args);
    //    if (format[strlen(format) - 1] != '\n') {
    //        fputc('\n', stderr);
    //    }
    //}

    //LeaveCriticalSection(&logLock);
    //if (!initialized) LogInit();

    //// ��ȡ��ǰʱ��
    //time_t now = time(NULL);
    //struct tm tm_info;
    //localtime_s(&tm_info, &now); // Windows��ȫ�汾
    //char timestamp[32];
    //strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_info);

    //// ��ʽ����Ϣ
    //char message[4096];
    //va_list args;
    //va_start(args, format);
    //vsnprintf(message, sizeof(message), format, args);
    //va_end(args);
    //message[sizeof(message) - 1] = '\0';

    //// �̰߳�ȫд��
    //if (TryEnterCriticalSection(&logLock)) {
    //    if (logFile) {
    //        // ����ļ���С����ƽ̨������
    //        fseek(logFile, 0, SEEK_END);
    //        long file_size = ftell(logFile);
    //        fseek(logFile, 0, SEEK_SET);

    //        if (file_size > 100 * 1024 * 1024) {
    //            fclose(logFile);
    //            rename("time_info_current.log", "time_info_archive.log");
    //            logFile = fopen("time_info_current.log", "a");
    //        }

    //        if (logFile) {
    //            fprintf(logFile, "[%s] %s\n", timestamp, message);
    //            fflush(logFile);
    //        }
    //    }
    //    LeaveCriticalSection(&logLock);
    //}
    //else {
    //    OutputDebugStringA(message);
    //}
} 
void LogClose() {
    if (logFile) {
        fclose(logFile);
        logFile = NULL;
    }
    DeleteCriticalSection(&logLock);
}
static void FreeResources() {
    LogMessage("FreeResources��ʼ...");
    LTC_State* state = GetState();
    if (!state) {
        LogMessage("FreeResources: ��״̬������");
        return;
    }
    if (state && state->running) {
	    LTC_Stop();
    }

    // 1. ֹͣ�̣߳�ȷ����Ƶ�ص����ٴ�����
    if (state->running) {
        LogMessage("ֹͣ��Ƶ�߳�...");
        state->running = 0;
        if (g_thread.hThread) {
            WaitForSingleObject(g_thread.hThread, 1000);
            CloseHandle(g_thread.hThread);
            g_thread.hThread = NULL;
        }
    }

    // 2. ������Ƶ�豸
    if (state->hWaveOut) {
        LogMessage("������Ƶ�豸...");
        waveOutReset(state->hWaveOut); // ����ֹͣ����

        // ��ȫ�ͷ���Ƶ��
        if (state->blocks && state->blockCount > 0) {
            for (int i = 0; i < state->blockCount; i++) {
                if (!state->blocks[i]) continue;

                // ȡ��prepare�����Դ���
                if (state->blocks[i]->dwFlags & WHDR_PREPARED) {
                    waveOutUnprepareHeader(state->hWaveOut, state->blocks[i], sizeof(WAVEHDR));
                }

                // �ͷ��ڴ�
                free(state->blocks[i]->lpData);
                free(state->blocks[i]);
            }
            free(state->blocks);
            state->blocks = NULL;
        }

        waveOutClose(state->hWaveOut);
        state->hWaveOut = NULL;
    }

    // 3. ���������
    if (state->encoder) {
        ltc_encoder_free(state->encoder);
        state->encoder = NULL;
    }

    // 4. ɾ���ٽ������ͷ�״̬
    DeleteCriticalSection(&state->cs);
    EnterCriticalSection(&g_stateLock);
    free(g_state);
    g_state = NULL;
    LeaveCriticalSection(&g_stateLock);

    LogMessage("FreeResources���");
    //// ������Ƶ��Դ
    //if (state->hWaveOut) {
    //    waveOutReset(state->hWaveOut);
    //    for (int i = 0; i < state->blockCount; i++) {
    //        if (state->blocks[i]) {
    //            waveOutUnprepareHeader(state->hWaveOut, state->blocks[i], sizeof(WAVEHDR));
    //            free(state->blocks[i]->lpData);
    //            free(state->blocks[i]);
    //        }
    //    }
    //    waveOutClose(state->hWaveOut);
    //}
    // ������Ƶ��Դ�İ�ȫ�汾
    if (state->hWaveOut) {
        // 1. ��ֹͣ����
        waveOutReset(state->hWaveOut);

        // 2. �ȴ����п����
        for (int i = 0; i < state->blockCount; i++) {
            if (state->blocks[i]) {
                // �ȴ��鲻�ٴ��ڲ���״̬
                while (state->blocks[i]->dwFlags & WHDR_INQUEUE) {
                    Sleep(10);
                }

                // ��ȫ unprepare
                if (state->blocks[i]->dwFlags & WHDR_PREPARED) {
                    MMRESULT res = waveOutUnprepareHeader(
                        state->hWaveOut,
                        state->blocks[i],
                        sizeof(WAVEHDR));

                    if (res != MMSYSERR_NOERROR) {
                        LogMessage("Warning: waveOutUnprepareHeader failed for block %d (error %d)",
                            i, res);
                    }
                }

                // �ͷ��ڴ�
                if (state->blocks[i]->lpData) {
                    free(state->blocks[i]->lpData);
                    state->blocks[i]->lpData = NULL;
                }

                free(state->blocks[i]);
                state->blocks[i] = NULL;  // ��Ҫ��ָ����NULL
            }
        }

        // 3. ���ر��豸
        MMRESULT closeRes = waveOutClose(state->hWaveOut);
        if (closeRes != MMSYSERR_NOERROR) {
            LogMessage("Warning: waveOutClose failed (error %d)", closeRes);
        }
        state->hWaveOut = NULL;  // ��Ҫ�������NULL
    }
    LogMessage("FreeResources,6\n");
    // ����������Դ
    if (state->encoder) ltc_encoder_free(state->encoder);
    DeleteCriticalSection(&state->cs);

    EnterCriticalSection(&g_stateLock);
    free(g_state);
    g_state = NULL;
    LeaveCriticalSection(&g_stateLock);

    LogMessage("FreeResources,7 end\n");
	//if (!g_state) return; // ��ǰ����
	//// ��ֹͣ�߳�
	//if (g_state && g_state->running) {
	//	LTC_Stop();
	//}

	//// �����߳���Դ
	//if (g_thread.hThread) {
	//	CloseHandle(g_thread.hThread);
	//	g_thread.hThread = NULL;
	//}
	//if (!g_state) return;

	//// �ͷ���Ƶ��
	//if (g_state->blocks) {
	//	for (int i = 0; i < g_state->blockCount; i++) {
	//		if (g_state->blocks[i]->lpData) free(g_state->blocks[i]->lpData);
	//		free(g_state->blocks[i]);
	//	}
	//	free(g_state->blocks);
	//}

	//// �ͷ�������Դ
	//if (g_state->encoder) ltc_encoder_free(g_state->encoder);
	//if (g_state->hWaveOut) waveOutClose(g_state->hWaveOut);
	//DeleteCriticalSection(&g_state->cs);
	//free(g_state);
	//g_state = NULL;
}



// ��������������ʱ���ַ���
static int ParseTime(const char* str, int* time) { 
    if (!str || !time) return 0;

    char buf[32];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // �滻���зָ���Ϊð��
    for (char* p = buf; *p; ++p) {
        if (*p == '-' || *p == ' ' || *p == ';') *p = ':';
    }
     
    return sscanf(buf, "%d:%d:%d:%d",
        &time[0], &time[1],
        &time[2], &time[3]) == 4;
}
// ��Ƶ�ص�����
void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2) { 
    if (uMsg != WOM_DONE) return; 
    // �̰߳�ȫ����g_state
    EnterCriticalSection(&g_stateLock);
    if (g_state) {
        InterlockedIncrement(&g_state->waveFreeBlockCount);
    }
    LeaveCriticalSection(&g_stateLock);


}

// ��ʼ��ʵ��
__declspec(dllexport) int __stdcall LTC_Init(const char* startTime, const char* endTime, const char* fps) {
    if (g_state) {
        LogMessage("��������LTC״̬...");
        LTC_Cleanup(); //  ����ӿ�
    }

    g_state = (LTC_State*)calloc(1, sizeof(LTC_State));
    if (!g_state) return -1;
    // ��ʼ���ٽ�������������������֮ǰ��
    InitializeCriticalSection(&g_state->cs);
    LogMessage("1\n");

    LogMessage("startTime %s ,endtime %s \n", startTime, endTime);
 
      
	g_state->running = 0;
	g_state->fps = fps;


    if (LTC_SetStopTime(endTime) != 0) {
        FreeResources();
        return -5;
    } 

	InitializeCriticalSection(&g_state->cs);

	// ��ʼ��������
	g_state->encoder = ltc_encoder_create(48000, atof(fps),
		atof(fps) == 25 ? LTC_TV_625_50 : LTC_TV_525_60, LTC_USE_DATE);
	if (!g_state->encoder) return -2;

	// ���ó�ʼʱ��
	int start[4];
	if (!ParseTime(startTime, start)) return -3;

	SMPTETimecode st = { 0 };
	st.hours = start[0];
	st.mins = start[1];
	st.secs = start[2];
	st.frame = start[3];
	ltc_encoder_set_timecode(g_state->encoder, &st);

	// ��ʼ����Ƶ�飨ԭAllocateBlocks�߼���
	g_state->blockCount = 8; // ˫����
	g_state->framesPerBlock = 1; // ÿ��֡��
	int blockSize = ltc_encoder_get_buffersize(g_state->encoder) * g_state->framesPerBlock;

	g_state->blocks = (WAVEHDR**)calloc(g_state->blockCount, sizeof(WAVEHDR*));
	for (int i = 0; i < g_state->blockCount; i++) {
		g_state->blocks[i] = (WAVEHDR*)calloc(1, sizeof(WAVEHDR));
		g_state->blocks[i]->lpData = (char*)calloc(blockSize, 1);
		g_state->blocks[i]->dwBufferLength = blockSize;
	}

	// ��ʼ����Ƶ���
	WAVEFORMATEX wfx = {
		.wFormatTag = WAVE_FORMAT_PCM,
		.nChannels = 1,
		.nSamplesPerSec = 48000,
		.wBitsPerSample = 8,
		.nBlockAlign = 1,
		.nAvgBytesPerSec = 48000
	};

	if (waveOutOpen(&g_state->hWaveOut, WAVE_MAPPER, &wfx,
		(DWORD_PTR)WaveOutProc,
		(DWORD_PTR)&g_state->waveFreeBlockCount,
		CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
		return -4;
	}

	return 0;
}
// ����ֹͣʱ�䣨�̰߳�ȫ��
__declspec(dllexport) int __stdcall LTC_SetStopTime(const char* stopTime) {
	if (!g_state) return -1;
    EnterCriticalSection(&g_state->cs);  
    LogMessage("����ֹͣʱ��: %s", stopTime);
	int newTime[4];
    if (!ParseTime(stopTime, newTime)) {
        LeaveCriticalSection(&g_state->cs);
        return -2;
    }
    strncpy(g_state->endTime, stopTime, sizeof(g_state->endTime) - 1);
    g_state->endTime[sizeof(g_state->endTime) - 1] = '\0';
    memcpy(g_state->stopTime, newTime, sizeof(newTime));
    g_state->stopTimeUpdated = 1;  // ���ø��±�־

    LeaveCriticalSection(&g_state->cs);
	return 0;
}

__declspec(dllexport) int __stdcall LTC_Stop() {
	if (!g_state || !g_state->running) return -1;

	g_state->running = 0;
	// �ȴ��߳��˳������1�룩
	if (g_thread.hThread) {
		WaitForSingleObject(g_thread.hThread, 1000);
		CloseHandle(g_thread.hThread);
		g_thread.hThread = NULL;
	}

	// ȡ������δ��ɵ���Ƶ��
	for (int i = 0; i < g_state->blockCount; i++) {
		if (g_state->blocks[i]->dwFlags & WHDR_INQUEUE) {
			waveOutReset(g_state->hWaveOut);
			break;
		}
	}

	return 0;
}

__declspec(dllexport) void __stdcall LTC_Cleanup() {
    LogMessage("xojo set ltcCleanip\n");
	FreeResources();
} 

static int TimeToFrames(int hours, int mins, int secs, int frames, double fps) {
    LogMessage("TimeToFrames  %d-%d-%d-%d   int %d \n", hours, mins, mins, frames,fps);
    return frames +
        (int)(secs * fps) +
        (int)(mins * 60 * fps) +
        (int)(hours * 3600 * fps);
}
//static unsigned __stdcall LTC_Thread(void* param) {
//    if (!param) return -1;  
//    ThreadParams* thread = (ThreadParams*)param;
//    LTC_State* state = thread->state;
//
//    const int bufferSize = ltc_encoder_get_buffersize(state->encoder);
//    int currentBlock = 0;
//    int values[4] = { 0 };
//    DWORD frameInterval = (DWORD)(1000.0 / atof(state->fps)); // ����֡�ʼ�����(ms)
//
//    // ��������ʱ����
//    char copy[20];
//    strncpy(copy, state->endTime, sizeof(copy) - 1);
//    copy[sizeof(copy) - 1] = '\0';
//    char* token = strtok(copy, ":-");
//    for (int i = 0; i < 4 && token != NULL; i++) {
//        values[i] = atoi(token);
//        token = strtok(NULL, ":-");
//    }
//
//    // ׼��������Ƶ��
//    for (int i = 0; i < state->blockCount; i++) {
//        if (waveOutPrepareHeader(state->hWaveOut, state->blocks[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
//            // ������
//            state->running = 0;
//            return -1;
//        }
//    }
//
//    DWORD lastFrameTime = GetTickCount();
//    int currentStopTime[4];
//    while (state->running) {
//        DWORD currentTime = GetTickCount(); 
//        lastFrameTime = currentTime;
//
//        // ��鵱ǰ���Ƿ��Ѳ������
//        if (!(state->blocks[currentBlock]->dwFlags & WHDR_INQUEUE)) {
//            // ��յ�ǰ��
//            memset(state->blocks[currentBlock]->lpData, 0, bufferSize);
//
//            // ���ɵ�֡LTC����
//            ltc_encoder_encode_frame(state->encoder);
//
//            int len;
//            ltcsnd_sample_t* buf = ltc_encoder_get_bufptr(state->encoder, &len, 1);
//            memcpy(state->blocks[currentBlock]->lpData, buf, len);
//            state->blocks[currentBlock]->dwBufferLength = len;
//
//            // д����Ƶ�豸
//            MMRESULT res = waveOutWrite(state->hWaveOut, state->blocks[currentBlock], sizeof(WAVEHDR));
//            if (res != MMSYSERR_NOERROR) {
//                // ������
//                state->running = 0;
//                break;
//            }
//
//            if (state->stopTimeUpdated) {
//                EnterCriticalSection(&state->cs);
//                memcpy(currentStopTime, state->stopTime, sizeof(currentStopTime));
//                state->stopTimeUpdated = 0;
//                LeaveCriticalSection(&state->cs); 
//                LogMessage("set end %d-%d-%d-%d\n", state->stopTime[0], state->stopTime[1], state->stopTime[2], state->stopTime[3]);
//            }
//             
//            // ��ȡ��ǰʱ����
//            SMPTETimecode stt;
//            ltc_encoder_get_timecode(state->encoder, &stt);
//            int currentFrames;
//            int stopFrames;
//            EnterCriticalSection(&state->cs);
//            int shouldStop;
//            double fps = atof(state->fps);
//            int validStopTime = (state->stopTime[0] >= 0 &&
//                state->stopTime[1] >= 0 &&
//                state->stopTime[2] >= 0 &&
//                state->stopTime[3] >= 0 &&
//                fps >= 0);
//            LogMessage("new %d-%d-%d-%d\n", stt.hours, stt.mins, stt.secs, stt.frame); 
//            if (validStopTime) { 
//                currentFrames = TimeToFrames(stt.hours, stt.mins, stt.secs, stt.frame, fps);
//                stopFrames = TimeToFrames(state->stopTime[0], state->stopTime[1],
//                    state->stopTime[2], state->stopTime[3], fps);
//
//                // ��ǰ֡�� >= ֹͣ֡��ʱֹͣ
//                if (stopFrames != 0) {
//                    shouldStop = (currentFrames >= stopFrames);
//                }
//            }
//            else {
//                shouldStop = 0; // ��Ч��ֹͣʱ�䣬������ֹͣ
//            }
//
//            LeaveCriticalSection(&state->cs); 
//            if (shouldStop) {
//                state->running = 0;  
//                LogMessage("end%d-%d-%d-%d ,currentFrames %d stopFrames %d \n", state->stopTime[0], state->stopTime[1], state->stopTime[2], state->stopTime[3], currentFrames, stopFrames);
//                break;
//            }
//
//
//            // ����ʱ����
//            ltc_encoder_inc_timecode(state->encoder);
//
//            // �ƶ�����һ����
//            currentBlock++;
//            currentBlock %= state->blockCount;
//        }
//        else {
//            // û�п��ÿ飬���ݵȴ�
//            Sleep(5);
//        }
//    }
//    LogMessage("LTC_Thread end\n");
//    // ������Ƶ��
//    for (int i = 0; i < state->blockCount; i++) {
//        while (waveOutUnprepareHeader(state->hWaveOut, state->blocks[i], sizeof(WAVEHDR)) == WAVERR_STILLPLAYING) {
//            Sleep(10);
//        }
//    }
//    LogMessage("LTC_Thread end return \n");
//    return 0;
//}
int time_str_to_int(const char* time_str) {
    int time_int;
    sscanf(time_str, "%8d", &time_int); // ֱ�Ӷ�ȡΪ8λ����

    //LogMessage("time to int %d",time_int);
    return time_int;
}



static unsigned __stdcall LTC_Thread(void* param) {
    if (!param) return -1;

    ThreadParams* thread = (ThreadParams*)param;
    LTC_State* state = thread->state;
    const int bufferSize = ltc_encoder_get_buffersize(state->encoder);
    int currentBlock = 0;
    DWORD frameInterval = (DWORD)(1000.0 / atof(state->fps));

    // ��ȫ��������ʱ����
    int stopTime[4] = { 0 };
    {
        char copy[32];
        strncpy(copy, state->endTime, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';

        char* next = NULL;
        char* token = strtok_s(copy, ":-", &next);
        for (int i = 0; i < 4 && token; i++) {
            stopTime[i] = atoi(token);
            token = strtok_s(NULL, ":-", &next);
        }
    }
    // ׼����Ƶ�飨��������
    for (int i = 0; i < state->blockCount; i++) {
        if (!state->blocks[i]) continue;

        MMRESULT res = waveOutPrepareHeader(
            state->hWaveOut,
            state->blocks[i],
            sizeof(WAVEHDR));

        if (res != MMSYSERR_NOERROR) {
            LogMessage("waveOutPrepareHeader failed: %d", res);
            state->running = 0;
            return -1;
        }
    }
    int currentStopTime[4];
    LogMessage("LTC_Thread start\n");
    // ��ѭ��
    while (1) {
        // ��ȫ��ȡ����״̬
        EnterCriticalSection(&state->cs);
        BOOL running = state->running;
        LeaveCriticalSection(&state->cs);
        if (!running) break;

        // ����ǰ��Ƶ��
        WAVEHDR* block = state->blocks[currentBlock];
        if (!(block->dwFlags & WHDR_INQUEUE)) {
            // ����LTC����
            memset(block->lpData, 0, bufferSize);
            ltc_encoder_encode_frame(state->encoder);

            int len = 0;
            ltcsnd_sample_t* buf = ltc_encoder_get_bufptr(state->encoder, &len, 1);
            memcpy(block->lpData, buf, len);
            block->dwBufferLength = len;

            // д����Ƶ�豸
            MMRESULT res = waveOutWrite(state->hWaveOut, block, sizeof(WAVEHDR));
            if (res != MMSYSERR_NOERROR) {
                LogMessage("waveOutWrite failed: %d", res);
                break;
            }
            if (state->stopTimeUpdated) {
                EnterCriticalSection(&state->cs);
                memcpy(currentStopTime, state->stopTime, sizeof(currentStopTime));
                state->stopTimeUpdated = 0;
                LeaveCriticalSection(&state->cs);
                LogMessage("set end %d-%d-%d-%d\n", state->stopTime[0], state->stopTime[1], state->stopTime[2], state->stopTime[3]);
            }
            // ���ֹͣ�������̰߳�ȫ��
            EnterCriticalSection(&state->cs);
            SMPTETimecode stt;
            ltc_encoder_get_timecode(state->encoder, &stt);


            int shouldStop = 0;
            int currentFrames, stopFrames;  
            char* buffer[16] = {0};
            char* buffer1[16] = { 0 };
            snprintf(buffer, 16, "%02d%02d%02d%02d",
                stt.hours, stt.mins, stt.secs, stt.frame);
            snprintf(buffer1, 16, "%02d%02d%02d%02d",
                state->stopTime[0], state->stopTime[1], state->stopTime[2], state->stopTime[3]);




            int current = time_str_to_int(buffer); 
            currentFrames = current;
                
            int target = time_str_to_int(buffer1);
            stopFrames = target;
            shouldStop = (target > 0 && current >= target); 
            LeaveCriticalSection(&state->cs);

            if (shouldStop) {
                state->running = 0;
                LogMessage("end%d-%d-%d-%d ,currentFrames %d stopFrames %d \n", state->stopTime[0], state->stopTime[1], state->stopTime[2], state->stopTime[3], currentFrames, stopFrames);
                break;
            }
            ltc_encoder_inc_timecode(state->encoder);
            // �ƶ�����һ����
            currentBlock = (currentBlock + 1) % state->blockCount;
        }
        else {
            Sleep(5); // ����æ�ȴ�
        }
    }
    LogMessage("LTC_Thread end\n");
    // ��ȫ����
    if (state->hWaveOut) {
        for (int i = 0; i < state->blockCount; i++) {
            if (!state->blocks[i]) continue;

            while (waveOutUnprepareHeader(
                state->hWaveOut,
                state->blocks[i],
                sizeof(WAVEHDR)) == WAVERR_STILLPLAYING) {
                Sleep(10);
            }
        }
    }
    LogMessage("LTC_Thread end return \n");
    return 0;
}



__declspec(dllexport) int __stdcall LTC_Start() {
	if (!g_state || g_state->running) return -1;

	// ȷ��֮ǰ���߳����˳�
	if (g_thread.hThread) {
		WaitForSingleObject(g_thread.hThread, 1000);
		CloseHandle(g_thread.hThread);
		g_thread.hThread = NULL;
	}

	// ����ֹͣ��־
	g_state->running = 1;
	g_state->waveFreeBlockCount = g_state->blockCount;
    LogMessage("2222\n");
	// ���������߳�
	g_thread.state = g_state;
	g_thread.hThread = (HANDLE)_beginthreadex(
		NULL,                   // ��ȫ����
		0,                      // Ĭ�϶�ջ��С
		LTC_Thread,             // �̺߳���
		&g_thread,              // ����
		0,                      // ������־���������У�
		&g_thread.threadId      // �߳�ID
	);
    LogMessage("333\n");
	if (!g_thread.hThread) {
		g_state->running = 0;
		return -2; // �̴߳���ʧ��
	}

	return 0;
}


__declspec(dllexport) int __stdcall LTC_GetCurrentTime(char* buffer) {
	if (!g_state || !buffer) return -1;

	SMPTETimecode tc;
	EnterCriticalSection(&g_state->cs);
	ltc_encoder_get_timecode(g_state->encoder, &tc);
	LeaveCriticalSection(&g_state->cs);

	snprintf(buffer, 16, "%02d:%02d:%02d:%02d",
		tc.hours, tc.mins, tc.secs, tc.frame);
	return 0;
}

LTCEncoder* lGetLtcEncoder(const char* MyFps)
{
	LTCEncoder* encoder;
	//double num = strtod(str, NULL);
	double fps = strtod(MyFps, NULL);
	double sample_rate = 48000;
	encoder = ltc_encoder_create(sample_rate, fps, fps == 25 ? LTC_TV_625_50 : LTC_TV_525_60, LTC_USE_DATE);
	return encoder;
}

int lSetCurrentDateTime(SMPTETimecode* pst, LTCEncoder* pencoder, const char* StartValue, const char* EndValue)
{

	time_t t;
	struct tm* tmp;
	t = time(NULL);
	tmp = localtime(&t);
	char sene[4] = { 0 };
	char sene2[3] = { 0 };
	sprintf(sene, "%d", tmp->tm_year);
	memcpy(sene2, &sene[1], 2);
	const char timezone[6] = "+0200";
	strcpy(pst->timezone, timezone);
	pst->years = atoi(sene2);
	pst->months = tmp->tm_mon;
	pst->days = tmp->tm_mday; 

	char* token;
	char copy[20];
	int values[4];
	int i = 0;

	strcpy(copy, StartValue);

	token = strtok(copy, ":-");
	while (token != NULL && i < 4) {
		values[i] = atoi(token);
		token = strtok(NULL, ":-");
		i++;
	}
     
	pst->hours = values[0];
	pst->mins = values[1];
	pst->secs = values[2];
	pst->frame = values[3];
	ltc_encoder_set_timecode(pencoder, pst);
	return 0;
}

int lSetCurrentTime(SMPTETimecode* pst, LTCEncoder* pencoder)
{
	time_t t;
	struct tm* tmp;
	t = time(NULL);
	tmp = localtime(&t);
	pst->hours = tmp->tm_hour;
	pst->mins = tmp->tm_min;
	pst->secs = tmp->tm_sec;
	pst->frame = 0;
	ltc_encoder_set_timecode(pencoder, pst);
	return 0;
}

int writeAudioBlock(HWAVEOUT hWaveOut, WAVEHDR* header)
{

	LPTSTR hata = malloc(sizeof(char) * 100);
	MMRESULT back = NULL;
	if (header->dwFlags & WHDR_PREPARED)
	{
		back = waveOutUnprepareHeader(hWaveOut, header, sizeof(WAVEHDR));
	}
	back = waveOutPrepareHeader(hWaveOut, header, sizeof(WAVEHDR));
	back = waveOutWrite(hWaveOut, header, sizeof(WAVEHDR));
	if (back != MMSYSERR_NOERROR)
	{
		waveOutGetErrorText(back, hata, 100);
		printf(hata);
		free(hata);
		return 1;
	}

	EnterCriticalSection(&waveCriticalSection);
	waveFreeBlockCount--;
	LeaveCriticalSection(&waveCriticalSection);

	if (back == MMSYSERR_NOERROR)
		return 0;

}

WAVEHDR** AllocateBlocks(int count, int blockdatasize)
{
	WAVEHDR** blocks = NULL;
	WAVEHDR* block = NULL;
	blocks = calloc(count, sizeof(WAVEHDR*));
	for (int i = 0; i < count; i++)
	{
		blocks[i] = (WAVEHDR*)calloc(1, sizeof(WAVEHDR));
		blocks[i]->lpData = (LPSTR)calloc(blockdatasize, sizeof(char));
	}
	return blocks;
}

int LoadLtcData(WAVEHDR* block, LTCEncoder* pencoder, int blockDataSize, int vFrames)
{

	ltcsnd_sample_t* buf; // ����һ��ָ��LTC������ָ�����
	memset(block->lpData, 0, blockDataSize); // ����Ƶ������������ʼ��Ϊ��

	int len; // ����һ���������ڴ洢ÿ��LTC֡�ĳ���
	for (int i = 0; i < vFrames; i++) // ѭ������ָ��������LTC֡
	{
		ltc_encoder_encode_frame(pencoder); // �Ե�ǰʱ������б�������һ֡LTC����
		buf = ltc_encoder_get_bufptr(pencoder, &len, 1); // ��ȡ������LTC���ݺ��䳤��

		// ����ȡ����LTC���ݸ��Ƶ���Ƶ������������У�ÿ�θ��Ƶ���ʼλ�ø���֡���Ⱥ͵�ǰ֡����������õ�
		memcpy(block->lpData + (len * i), buf, len);

		ltc_encoder_inc_timecode(pencoder); // ����LTC�������е�ʱ���룬׼����һ֡�ı���

	}

	block->dwBufferLength = len * vFrames; // ������Ƶ������ݳ��ȣ���Ϊ��֡���ݳ��ȳ���֡��
	block->dwUser = 0; // ��dwUser�ֶ�����Ϊ0��ͨ�����ڴ洢�Զ�������
	return 0; // ����0����ʾ����LTC���ݵĲ���ִ�гɹ� 
}


