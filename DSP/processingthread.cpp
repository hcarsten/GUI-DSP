#include "processingthread.h"
#include <QMessageBox>
#include <stdio.h>
#include <QDebug>

//#define ZED

double *a; //coeficientii b
double *b; //coeficientii a
int np; //numarul de ploi
int deg; //gradul

class StageFilter
{
    qint16 delLeftIn[22];
    qint16 delLeftOut[22];
    qint16 delRightIn[22];
    qint16 delRightOut[22];
    qint16 aSumLeft;
    qint16 bSumLeft;
    qint16 aSumRight;
    qint16 bSumRight;

public:
    StageFilter()
    {
        aSumLeft = 0;
        bSumLeft = 0;
        aSumRight = 0;
        bSumRight = 0;
    }
    void startFilter(qint16 *in_buffer, qint16 *out_buffer, int vecsize)
    {
        for(int i=0; i<vecsize; i = i + 2)
        {
            //left channel++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
            delLeftIn[0] = in_buffer[i];

            aSumLeft = 0;

            for(int j=0; j<np; j++)
                aSumLeft += a[j]*delLeftIn[j];

            bSumLeft = 0;
            for(int j=1; j<np;j++)
                bSumLeft += b[j]*delLeftOut[j];

            out_buffer[i] = (qint16)(aSumLeft  + bSumLeft);

            for(int j=np-1; j>=1; j--)
                delLeftIn[j] = delLeftIn[j-1];

            delLeftOut[1] = out_buffer[i];
            for(int j=np-1; j>1; j--)
                delLeftOut[j] = delLeftOut[j-1];

            //right channel+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

            delRightIn[0] = in_buffer[i+1];

            aSumRight = 0;
            for(int j=0; j<np; j++)
                aSumRight += a[j]*delRightIn[j];

            bSumRight = 0;
            for(int j=1; j<np;j++)
                bSumRight += b[j]*delLeftOut[j];

            out_buffer[i+1] = (qint16)(aSumRight  + bSumRight);

            for(int j=np-1; j>=1; j--)
                delRightIn[j] = delRightIn[j-1];

            delRightOut[1] = out_buffer[i+1];
            for(int j=np-1; j>1; j--)
                delRightOut[j] = delRightOut[j-1];
        }
    }
};

ProcessingThread::ProcessingThread(double const* coefaa, double const* coefbb, const QString strr, const int& npp, const int& degg)
{
    a = new double[npp];
    b = new double[npp];

    stopProcessing = false;

    memcpy(a, coefaa, npp * sizeof(double));
    memcpy(b, coefbb, npp * sizeof(double));

    np = npp;
    deg = degg;

    filePath = strr;
}


void ProcessingThread::startProcessing()
{
    struct riff_wave_header riff_wave_header;
    struct chunk_header chunk_header;
    struct chunk_fmt chunk_fmt;
#ifndef ZED
    unsigned int device = 0;
    unsigned int card = 0;
    unsigned int period_size = 1024;
    unsigned int period_count = 4;
#endif
    int more_chunks = 1;

    stopProcessing = false;
    WAVFile = fopen(filePath.toAscii(), "rb"); //fisierul de intrare pe care se face procesarea de semnal
    if (!WAVFile)
    {
        //QMessageBox::warning(this, tr("DSP"), tr("Nu se poate accesa fisierul!"));
        return ;
    }

#ifdef ZED //deschide /dev/xillybus_audio/ pentru a scrie fisierul .wav (driverul de sunet) - play .wav
    FILE *file_out = fopen("out.wav", "wb");
    if (!file_out)
    {
        fprintf(stderr, "Unable to open file '%s'\n");
        return ;
    }
#endif

    fread(&riff_wave_header, sizeof(riff_wave_header), 1, WAVFile);
    if ((riff_wave_header.riff_id != ID_RIFF) || (riff_wave_header.wave_id != ID_WAVE))
    {
        //QMessageBox::warning(this, tr("DSP"), tr("Nu se poate accesa fisierul!"));
        fclose(WAVFile);
        return ;
    }

#ifdef ZED
    fwrite(&riff_wave_header, sizeof(riff_wave_header), 1, file_out);
#endif

    do
    {
        fread(&chunk_header, sizeof(chunk_header), 1, WAVFile);

        switch (chunk_header.id) {
        case ID_FMT:
            fread(&chunk_fmt, sizeof(chunk_fmt), 1, WAVFile);
            // If the format header is larger, skip the rest
            if (chunk_header.sz > sizeof(chunk_fmt))
                fseek(WAVFile, chunk_header.sz - sizeof(chunk_fmt), SEEK_CUR);
#ifdef ZED
            fwrite(&chunk_header, sizeof(chunk_header), 1, file_out);
            fwrite(&chunk_fmt, sizeof(chunk_fmt), 1, file_out);
#endif
            break;
        case ID_DATA:
        {
#ifdef ZED
            uint16_t a = 0;
            fwrite(&a, sizeof(uint16_t), 1, file_out);
            fwrite(&chunk_header, sizeof(chunk_header), 1, file_out);
#endif
            //Stop looking for chunks
            more_chunks = 0;
            break;
        }
        default:
            //Unknown chunk, skip bytes
            fseek(WAVFile, chunk_header.sz, SEEK_CUR);
        }
    } while (more_chunks);

#ifdef ZED
    play_sample(file_out);
#else
    play_sample(WAVFile, card, device, chunk_fmt.num_channels, chunk_fmt.sample_rate, chunk_fmt.bits_per_sample, period_size, period_count);
#endif

    fclose(WAVFile);

#ifdef ZED
    fclose(file_out);
#endif

    emit finished();
}

#ifdef ZED
void ProcessingThread::play_sample(FILE *file_out)
#else
void ProcessingThread::play_sample(FILE *file, unsigned int card, unsigned int device, unsigned int channels,
                                   unsigned int rate, unsigned int bits, unsigned int period_size,
                                   unsigned int period_count)
#endif
{
    qint16 *in_buffer;
    qint16 *out_buffer;
#ifdef ZED
    int size = 16384;
    int num_read;
#else
    struct pcm_config config;
    struct pcm *pcm;
    int num_read;
    int size; //dimensiunea bufferelor

    config.channels = channels;
    config.rate = rate;
    config.period_size = period_size;
    config.period_count = period_count;
    if (bits == 32)
        config.format = PCM_FORMAT_S32_LE;
    else if (bits == 16)
        config.format = PCM_FORMAT_S16_LE;
    config.start_threshold = 0;
    config.stop_threshold = 0;
    config.silence_threshold = 0;

    //if (!sample_is_playable(card, device, channels, rate, bits, period_size, period_count)) {
    //    return;
    // }

    pcm = pcm_open(card, device, PCM_OUT, &config);
    if (!pcm || !pcm_is_ready(pcm))
    {
        //QMessageBox::warning(this, tr("DSP"), tr("Nu se poate accesa dispozitivul!"));
        qDebug()<<"pcm is not ready!";
        return;
    }

    size = pcm_frames_to_bytes(pcm, pcm_get_buffer_size(pcm)); //size = buffer size (in bytes)
    qDebug()<<"buffer size [bytes]="<<size;
#endif

    in_buffer = (qint16*)malloc(size);
    if (!in_buffer)
    {
        //QMessageBox::warning(this, tr("DSP"), tr("Nu se poate aloca memorie pentru buffer!"));
        qDebug()<<"could not load buffer!";
        free(in_buffer);
#ifndef ZED
        pcm_close(pcm);
#endif
        return;
    }

    out_buffer = (qint16*)malloc(size);
    if (!out_buffer)
    {
        //QMessageBox::warning(this, tr("DSP"), tr("Nu se poate aloca memorie pentru buffer!"));
        qDebug()<<"could not load buffer!";
        free(out_buffer);
#ifndef ZED
        pcm_close(pcm);
#endif
        return;
    }

    //****************************************************************processing loop**********************
    StageFilter stage[deg];

    qDebug()<<"deg="<<deg;
    do
    {
        num_read = fread(in_buffer, sizeof(qint16), size/2, WAVFile);

        stage[0].startFilter(in_buffer, out_buffer, num_read);
        for(int i = 1; i<deg; i++)//aplic filtrarea in cascada
            stage[i].startFilter(out_buffer, out_buffer, num_read);

        if (num_read > 0)
        {
#ifdef ZED
            if (fwrite(out_buffer, sizeof(qint16), num_read, file_out) != num_read)
            {
                fprintf(stderr, "Error writing to outfile!\n");
                break;
            }
#else
            if (pcm_write(pcm, out_buffer, num_read*2))
            {
                fprintf(stderr, "Error playing sample\n");
                break;
            }
#endif
        }
    } while ((num_read > 0) && (stopProcessing == false));
    //*******************************************************************************************

    free(in_buffer);
    free(out_buffer);
#ifndef ZED
    pcm_close(pcm);
#endif
    qDebug()<<"End playing !";
}

void ProcessingThread::stopProcessingSlot()
{
    stopProcessing = true;
}
