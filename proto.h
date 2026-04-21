/**
 * @fisier proto.h
 * @pe scurt PCD T31 - gSOAP serviciu interface definition
 *
 * acest fisier este implicitul intrare la wsdl2h / soapcpp2 la generate
 * implicitul soap bindings (soapH.h, soapC.c, soapServer.c, etc.).
 *
 * serviciu: ServiciulVideoSpectrograma
 * Operations:
 *   - analizeazaAudio      : compute Mel spectrograma pentru an audio fisier
 *   - getJobStatus      : poll async job Status
 *   - extractVideoFrame : extract a cadru from a video fisier (basic op)
 *
 * Compile gSOAP bindings:
 *   soapcpp2 -c -s proto.h
 */

/* gSOAP namespace map */
//gSOAP ns serviciu nume: ServiciulVideoSpectrograma //gSOAP ns serviciu style: document //gSOAP ns serviciu encoding: literal //gSOAP ns serviciu location: http://localhost:8080/soap //gSOAP ns schema namespace: urn:ServiciulVideoSpectrograma 
/* cerere / raspuns mesaj types                                    */

/** cerere: analizeaza audio si produce spectrograma */
struct ns__AnalyzeAudioRequest {
    char*inputFilePath;    /**< cale la audio/video fisier on Server FS */
    int   targetSampleRate; /**< 0 = native; e.g. 22050               */
    int   nMels;            /**< Mel benzi; 0 = implicit (128)          */
    int   nFft;             /**< FFT dimensiune;  0 = implicit (2048)         */
    int   hopLength;        /**< Hop lungime; 0 = implicit (512)         */
};

/** raspuns: rezultate de spectrograma analysis */
struct ns__AnalyzeAudioResponse {
    int    statusCode;       /**< 0 = succes, non-zero = eroare         */
    char*statusMessage;    /**< "OK" or eroare description             */
    char*resultFilePath;   /**< cale la iesire binar spectrograma     */
    int    sampleRate;       /**< Actual esantion rata used               */
    double durationSeconds;  /**< lungime de audio analysed              */
    int    nMels;            /**< Mel benzi in iesire                   */
    int    nFrames;          /**< timp Cadre in iesire                 */
};

/** cerere: query an async job (future milestone) */
struct ns__JobStatusRequest {
    char*jobId;             /**< Opaque job identifier                 */
};

/** raspuns: job Status */
struct ns__JobStatusResponse {
    int   statusCode;        /**< 0=done, 1=running, 2=queued, -1=err  */
    char*statusMessage;     /**< Human-readable Status                 */
    char*resultFilePath;    /**< Non-NULL when statusCode == 0         */
};

/** cerere: extract a single video cadru */
struct ns__ExtractFrameRequest {
    char*videoFilePath;    /**< cale la intrare video fisier              */
    double timestampSeconds; /**< Timestamp la extract                  */
    char*outputImagePath;  /**< Where la scrie implicitul cadru PNG          */
};

/** raspuns: cadru extraction rezultat */
struct ns__ExtractFrameResponse {
    int   statusCode;        /**< 0 = succes                          */
    char*statusMessage;     /**< "OK" or eroare description            */
    int   frameWidth;        /**< Width de extracted cadru (pixels)    */
    int   frameHeight;       /**< Height de extracted cadru (pixels)   */
};

/* serviciu operation declarations                                      */

/**
 * @pe scurt Compute Mel spectrograma pentru an audio fisier.
 */
int ns__analyzeAudio(struct ns__AnalyzeAudioRequest*req,
                     struct ns__AnalyzeAudioResponse*res);

/**
 * @pe scurt Poll implicitul Status de an async procesare job.
 */
int ns__getJobStatus(struct ns__JobStatusRequest*req,
                     struct ns__JobStatusResponse*res);

/**
 * @pe scurt Extract a single cadru from a video fisier.
 */
int ns__extractVideoFrame(struct ns__ExtractFrameRequest*req,
                          struct ns__ExtractFrameResponse*res);