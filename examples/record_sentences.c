#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "portaudio.h"
#include "fvad.h"

/* #define SAMPLE_RATE  (17932) // Test failure to open with this value. */
#define SAMPLE_RATE  (16000)
#define FRAME_TIME  (30)  // ms
#define FRAMES_PER_BUFFER (SAMPLE_RATE / 1000 * FRAME_TIME)
#define NUM_SECONDS     (20)
#define NUM_FRAME  (SAMPLE_RATE * NUM_SECONDS)
#define NUM_CHANNELS    (1)
/* #define DITHER_FLAG     (paDitherOff) */
#define DITHER_FLAG     (0) /**/
/** Set to 1 if you want to capture the recording to a file. */

/* Select sample format. */
#define PA_SAMPLE_TYPE  paInt16
#define BITS_PER_SAMPLE 16
typedef short SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"

typedef struct
{
    int          frameIndex;  /* Index into sample array. */
    int          maxFrameIndex;
    SAMPLE      *recordedSamples;
}
paTestData;


// WAV 文件头结构
#pragma pack(push, 1)
typedef struct {
    char riff[4];                // "RIFF"
    uint32_t fileSize;           // 4 + (size of the rest of the file)
    char wave[4];                // "WAVE"
    char fmt[4];                 // "fmt "
    uint32_t fmtSize;            // 16 for PCM
    uint16_t audioFormat;        // PCM = 1
    uint16_t numChannels;        // Number of channels (1 for mono, 2 for stereo)
    uint32_t sampleRate;         // Sample rate (e.g., 44100)
    uint32_t byteRate;           // SampleRate * NumChannels * BitsPerSample / 8
    uint16_t blockAlign;         // NumChannels * BitsPerSample / 8
    uint16_t bitsPerSample;      // Bits per sample (e.g., 16)
    char data[4];                // "data"
    uint32_t dataSize;           // Number of bytes of audio data
} WaveHeader;
#pragma pack(pop)

// 保存原始音频数据为WAV文件
int save_wav(
        const char *filename,
        const int16_t *audioData,
        uint32_t sampleRate,
        uint16_t numChannels,
        uint16_t bitsPerSample,
        uint32_t numSamples
    )
{
    FILE *outFile = fopen(filename, "wb");
    if (!outFile) {
        printf("Error opening file for writing.\n");
        return -1;
    }

    // 计算文件大小
    uint32_t dataSize = numSamples * numChannels * bitsPerSample / 8;  // 数据大小
    uint32_t fileSize = 36 + dataSize;  // WAV文件总大小（文件头 + 数据）

    // 创建 WAV 文件头
    WaveHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.riff, "RIFF", 4);
    header.fileSize = fileSize - 8; // 文件头大小减去 "RIFF" 和文件大小字段
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    header.fmtSize = 16;  // 对于PCM，fmt chunk的大小是16
    header.audioFormat = 1;  // PCM 格式
    header.numChannels = numChannels;
    header.sampleRate = sampleRate;
    header.byteRate = sampleRate * numChannels * bitsPerSample / 8;
    header.blockAlign = numChannels * bitsPerSample / 8;
    header.bitsPerSample = bitsPerSample;
    memcpy(header.data, "data", 4);
    header.dataSize = dataSize;

    // 写入文件头
    fwrite(&header, sizeof(WaveHeader), 1, outFile);

    // 写入音频数据
    fwrite(audioData, sizeof(int16_t), numSamples * numChannels, outFile);

    fclose(outFile);
    printf("WAV file saved successfully to %s\n", filename);
    return 0;
}

/* This routine will be called by the PortAudio engine when audio is needed.
** It may be called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int recordCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    paTestData *data = (paTestData*)userData;
    const SAMPLE *rptr = (const SAMPLE*)inputBuffer;
    SAMPLE *wptr = &data->recordedSamples[data->frameIndex * NUM_CHANNELS];
    long framesToCalc;
    long i;
    int finished;
    unsigned long framesLeft = data->maxFrameIndex - data->frameIndex;

    (void) outputBuffer; /* Prevent unused variable warnings. */
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;

    if( framesLeft < framesPerBuffer )
    {
        framesToCalc = framesLeft;
        finished = paComplete;
    }
    else
    {
        framesToCalc = framesPerBuffer;
        finished = paContinue;
    }

    if( inputBuffer == NULL )
    {
        for( i=0; i<framesToCalc; i++ )
        {
            *wptr++ = SAMPLE_SILENCE;  /* left */
            if( NUM_CHANNELS == 2 ) *wptr++ = SAMPLE_SILENCE;  /* right */
        }
    }
    else
    {
        for( i=0; i<framesToCalc; i++ )
        {
            *wptr++ = *rptr++;  /* left */
            if( NUM_CHANNELS == 2 ) *wptr++ = *rptr++;  /* right */
        }
    }
    data->frameIndex += framesToCalc;
    return finished;
}

int main()
{
    PaStreamParameters  inputParameters,
                        outputParameters;
    PaStream*           stream;
    PaError             err = paNoError;
    paTestData          data;
    int                 i;
    int                 totalFrames;
    int                 numSamples;
    int                 numBytes;
    SAMPLE              max, val;
    double              average;

    printf("patest_record.c\n"); fflush(stdout);

    data.maxFrameIndex = totalFrames = NUM_FRAME; /* Record for a few seconds. */
    data.frameIndex = 0;
    numSamples = totalFrames * NUM_CHANNELS;
    numBytes = numSamples * sizeof(SAMPLE);
    data.recordedSamples = (SAMPLE *) malloc( numBytes ); /* From now on, recordedSamples is initialised. */
    if( data.recordedSamples == NULL )
    {
        printf("Could not allocate record array.\n");
        goto done;
    }
    for( i=0; i<numSamples; i++ ) data.recordedSamples[i] = 0;

    err = Pa_Initialize();
    if( err != paNoError ) goto done;

    inputParameters.device = Pa_GetDefaultInputDevice(); /* default input device */
    if (inputParameters.device == paNoDevice) {
        fprintf(stderr,"Error: No default input device.\n");
        goto done;
    }
    inputParameters.channelCount = NUM_CHANNELS;
    inputParameters.sampleFormat = PA_SAMPLE_TYPE;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    /* Record some audio. -------------------------------------------- */
    err = Pa_OpenStream(
              &stream,
              &inputParameters,
              NULL,                  /* &outputParameters, */
              SAMPLE_RATE,
              FRAMES_PER_BUFFER,
              paClipOff,      /* we won't output out of range samples so don't bother clipping them */
              recordCallback,
              &data );
    if( err != paNoError ) goto done;

    err = Pa_StartStream( stream );
    if( err != paNoError ) goto done;
    printf("\n=== Now recording!! Please speak into the microphone. ===\n"); fflush(stdout);

    Fvad *vad = fvad_new();
    if (!vad) {
        fprintf(stderr, "out of memory\n");
        goto done;
    }

    if (fvad_set_sample_rate(vad, SAMPLE_RATE) < 0) {
        fprintf(stderr, "invalid sample rate: %d Hz\n", SAMPLE_RATE);
        goto done;
    }

    struct timespec currentTime;
    clock_gettime(CLOCK_MONOTONIC, &currentTime);
    double currentTimeSec = currentTime.tv_sec + currentTime.tv_nsec / 1e9;
    double activeTimeSec = currentTimeSec; // 初始化为当前时间
    int processIndex = 0; 
    int vadres = 1;
    while (vadres == 1 || currentTimeSec < activeTimeSec + 0.5)
    {
        Pa_Sleep(FRAME_TIME);
        clock_gettime(CLOCK_MONOTONIC, &currentTime);
        currentTimeSec = currentTime.tv_sec + currentTime.tv_nsec / 1e9;
        if (data.frameIndex < processIndex + FRAMES_PER_BUFFER) {
            continue;
        }

        printf("vadres = %d, index = %d\n", vadres, data.frameIndex ); fflush(stdout);
        vadres = fvad_process(vad,
                              data.recordedSamples + processIndex,
                              FRAMES_PER_BUFFER);
        processIndex += FRAMES_PER_BUFFER;

        if (vadres == 1) {
            activeTimeSec = currentTimeSec;
        }
    }
    if( err < 0 ) goto done;

    err = Pa_CloseStream( stream );
    if( err != paNoError ) goto done;

    save_wav("recorded.wav",
            data.recordedSamples,
            SAMPLE_RATE,
            NUM_CHANNELS,
            BITS_PER_SAMPLE,
            data.frameIndex);

done:
    Pa_Terminate();
    if( data.recordedSamples )       /* Sure it is NULL or valid. */
        free( data.recordedSamples );
    if( err != paNoError )
    {
        fprintf( stderr, "An error occurred while using the portaudio stream\n" );
        fprintf( stderr, "Error number: %d\n", err );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
        err = 1;          /* Always return 0 or 1, but no other return codes. */
    }
    return err;
}
