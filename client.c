/**
 * PCD T31 - SOAP Client pentru analiza spectrogramelor
 * 
 * Conecteaza la server si apeleaza serviciul de analiza audio.
 * 
 * Demonstreaza:
 *   - Utilizarea gSOAP client proxy
 *   - Parsarea argumentelor din linia de comanda (getopt)
 *   - Accesul la variabile de mediu (getenv)
 *   - Configurare via libconfig
 * 
 * Utilizare:
 *   ./inetclient -s http://localhost:8080/soap -f /path/to/audio.wav
 *   PCD_SERVER=http://myserver:9090/soap ./inetclient -f audio.wav
 */

#include <stdio.h>  /* Standard I/O */
#include <stdlib.h> /* Memory allocation and utilities */
#include <string.h> /* String manipulation */
#include <getopt.h>

/* gSOAP generated headers */
#include "soapH.h"
#include "proto.nsmap"

/* Default endpoint - can be overridden via PCD_SERVER env var */
#define DEFAULT_ENDPOINT "http://localhost:8080/soap"
#define ENV_SERVER_KEY   "PCD_SERVER"

/* Print usage information */
static void print_usage(const char*prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  -s URL    SOAP server endpoint (default: " DEFAULT_ENDPOINT ")\n"
        "  -f FILE   Audio/video file to analyse\n"
        "  -m MELS   Number of Mel bands (default: 128)\n"
        "  -r SR     Target sample rate   (default: 22050)\n"
        "  -h        Show this help\n"
        "\n"
        "Environment:\n"
        "  PCD_SERVER   Overrides the default server endpoint\n",
        prog);
}

int main(int argc, char*argv[])
{
    /* Check for server override from environment */
    const char*env_server=getenv(ENV_SERVER_KEY);

    const char*endpoint=env_server ? env_server : DEFAULT_ENDPOINT;
    const char*file_path=NULL;
    int n_mels=128;
    int sample_rate=22050;

    /* Parse command-line arguments */
    int opt;
    while ((opt=getopt(argc, argv, "s:f:m:r:h"))!=-1) {
        switch (opt) {
        case 's': endpoint=optarg;break;
        case 'f': file_path=optarg;break;
        case 'm': n_mels=atoi(optarg); break;
        case 'r': sample_rate=atoi(optarg); break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    if (!file_path) {
        fprintf(stderr, "[client] Error: -f <file> is required\n\n");
        print_usage(argv[0]);
        return 1;
    }

    fprintf(stdout, "[client] Connecting to: %s\n", endpoint);
    fprintf(stdout, "[client] File         : %s\n", file_path);
    fprintf(stdout, "[client] Mel bands    : %d\n", n_mels);
    fprintf(stdout, "[client] Sample rate  : %d Hz\n", sample_rate);

    /* Initialize SOAP runtime */
    struct soap soap;
    soap_init(&soap);

    /* Build request structure */
    struct ns__AnalyzeAudioRequest  req;
    struct ns__AnalyzeAudioResponse res;

    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));

    req.inputFilePath=(char *)file_path;
    req.targetSampleRate=sample_rate;
    req.nMels=n_mels;
    req.nFft=2048;
    req.hopLength=512;

    /* Make SOAP call */
    int rc=soap_call_ns__analyzeAudio(&soap, endpoint, NULL, &req, &res);

    if (rc!=SOAP_OK) {
        soap_print_fault(&soap, stderr);
        soap_end(&soap);
        soap_done(&soap);
        return 1;
    }

    /* Display results */
    fprintf(stdout, "\n[client] === Server Response ===\n");
    fprintf(stdout, "[client] Status     : %d (%s)\n",
            res.statusCode, res.statusMessage ? res.statusMessage : "");

    if (res.statusCode==0) {
        fprintf(stdout, "[client] Result file: %s\n",
                res.resultFilePath ? res.resultFilePath : "<none>");
        fprintf(stdout, "[client] Sample rate: %d Hz\n",  res.sampleRate);
        fprintf(stdout, "[client] Duration   : %.2f s\n", res.durationSeconds);
        fprintf(stdout, "[client] Mel bands  : %d\n",     res.nMels);
        fprintf(stdout, "[client] Time frames: %d\n",     res.nFrames);
    }

    /* Cleanup */
    soap_end(&soap);
    soap_done(&soap);

    return (res.statusCode==0) ? EXIT_SUCCESS : EXIT_FAILURE;
}