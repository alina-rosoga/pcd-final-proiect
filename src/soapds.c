/**
 * PCD T31 - gSOAP Service Operation Implementations
 * 
 * Each function corresponds to a WSDL operation declared in proto.h.
 * These are the "server-side" stubs called by soap_serve() after
 * gSOAP parses an incoming SOAP request.
 * 
 * NOTE: The actual heavy processing is delegated to processing.c,
 *       which forks a child process so crashes are isolated.
 */

#include <stdio.h>
/* pentru functii de intrare/iesire standard */
#include <string.h>
/* pentru manipularea sirurilor de caractere */
#include <unistd.h>
/* pentru functii POSIX de sistem */

/* gSOAP generated headers */
#include "soapH.h"
/* pentru anteturile generate de gSOAP */
#include "proto.nsmap"
/* pentru maparea namespace-urilor in gSOAP */

/* Project headers */
#include "processing.h"
/* pentru functii de procesare audio */
#include "server.h"
/* pentru definitii ale serverului */

/* Global config (extern from Server.c) */
extern server_config_t g_cfg;

/* Helper: construieste iesire fisier cale                                      */
static void build_output_path(char*buf, size_t bufsz,
                               const char*outdir,
                               const char*basename_hint)
{
    /* Use PID + a counter pentru unique iesire filenames */
    static int counter=0;
    (void)snprintf(buf, bufsz, "%s/%s_%d_%d.bin",
             outdir, basename_hint, (int)getpid(), counter++);
}

/* ns__analyzeAudio                                                   */
int ns__analyzeAudio(struct soap*soap,
                     struct ns__AnalyzeAudioRequest*req,
                     struct ns__AnalyzeAudioResponse*res)
{
    if (!req || !req->inputFilePath) {
        res->statusCode=-1;
        res->statusMessage=soap_strdup(soap, "Missing inputFilePath");
        return SOAP_OK;
    }

    /* construieste cerere pentru procesare module */
    proc_request_t preq;
    (void)memset(&preq, 0, sizeof(preq));

    (void)memcpy(preq.input_path, req->inputFilePath, MAX_PATH_LEN - 1);
    preq.input_path[MAX_PATH_LEN - 1]='\0';

    build_output_path(preq.output_path, MAX_PATH_LEN,
                      g_cfg.output_dir, "spec");

    preq.target_sr=req->targetSampleRate > 0 ? req->targetSampleRate
                                                 : DEFAULT_SR;
    preq.n_fft=req->nFft      > 0 ? req->nFft      : DEFAULT_N_FFT;
    preq.hop_length=req->hopLength > 0 ? req->hopLength : DEFAULT_HOP_LENGTH;
    preq.n_mels=req->nMels     > 0 ? req->nMels     : DEFAULT_N_MELS;

    /* Delegate la fork()ed worker */
    proc_result_t pres;
    (void)memset(&pres, 0, sizeof(pres));

    int ret_code=process_spectrogram(&preq, &pres);

    res->statusCode=(ret_code==0) ? 0 : -1;
    res->statusMessage=soap_strdup(soap, pres.error_msg);

    if (ret_code==0) {
        res->resultFilePath=soap_strdup(soap, pres.output_path);
        res->sampleRate=pres.sample_rate;
        res->durationSeconds=pres.duration_s;
        res->nMels=pres.n_mels;
        res->nFrames=pres.n_frames;
    } else {
        res->resultFilePath=soap_strdup(soap, "");
    }

    return SOAP_OK;
}

/* ns__getJobStatus  (stub pentru async milestone)                       */
int ns__getJobStatus(struct soap*soap,
                     struct ns__JobStatusRequest*req,
                     struct ns__JobStatusResponse*res)
{
    (void)soap;
    /* Stub: synchronous-only in milestone 1 */
    res->statusCode=2; /* 2 = not implemented / unknown */
    res->statusMessage=soap_strdup(soap, "Async jobs not yet implemented");
    res->resultFilePath=soap_strdup(soap, "");
    (void)req;
    return SOAP_OK;
}

/* ns__extractVideoFrame                                              */
int ns__extractVideoFrame(struct soap*soap,
                          struct ns__ExtractFrameRequest*req,
                          struct ns__ExtractFrameResponse*res)
{
    /*
     * milestone 1 stub.
     * Full implementation will use LibrosaC / libav pentru video demuxing.
     * implicitul Server NEVER proxies implicitul cadru bytes - it writes la implicitul FS
     * si returns implicitul iesire cale only.
     */
    if (!req || !req->videoFilePath) {
        res->statusCode=-1;
        res->statusMessage=soap_strdup(soap, "Missing videoFilePath");
        return SOAP_OK;
    }

    res->statusCode=1;  /* 1 = not fully implemented */
    res->statusMessage=soap_strdup(soap,
        "extractVideoFrame stub: full impl in Milestone 2");
    res->frameWidth=0;
    res->frameHeight=0;

    return SOAP_OK;
}
