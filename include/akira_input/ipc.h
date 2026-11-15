
#ifndef AKIRA_INPUT_IPC_H
#define AKIRA_INPUT_IPC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AKIRA_INPUT_IPC_SERVICE_NAME "akira:i"
#define AKIRA_INPUT_IPC_API_VERSION  20u

#define AKIRA_INPUT_ERR_BAD_REQUEST  0x1966B
#define AKIRA_INPUT_ERR_UNAVAILABLE  0x1A66B
#define AKIRA_INPUT_ERR_NO_STATE     0x1AE6B
#define AKIRA_INPUT_ERR_NO_PROFILE   0x1B26B
#define AKIRA_INPUT_ERR_FULL         0x1B66B

typedef enum {
    AkiraInputCmd_GetApiVersion   = 0,
    AkiraInputCmd_GetStatus       = 1,
    AkiraInputCmd_Subscribe       = 2,
    AkiraInputCmd_Unsubscribe     = 3,
    AkiraInputCmd_GetTriggerState = 4,
    AkiraInputCmd_GetRawReport    = 5,
    AkiraInputCmd_ListDevices     = 6,
    AkiraInputCmd_WriteOutputReport = 13,
    AkiraInputCmd_ProbeAudio        = 14,
    AkiraInputCmd_ProbeDirectWrite  = 15,
    AkiraInputCmd_GetDescriptor     = 16,
    AkiraInputCmd_ZeroRetransmit    = 17,
    AkiraInputCmd_ExternalControl   = 18,
    AkiraInputCmd_GetDirectOutput   = 19,
    AkiraInputCmd_SetDirectOutput   = 20,
} AkiraInputCmd;

typedef struct {
    uint8_t  bt_addr[6];
    uint8_t  method;
    uint8_t  reserved;
} AkiraInputDirectProbe;

typedef struct {
    uint32_t rc;
    uint32_t init_rc;
} AkiraInputDirectResult;

typedef enum {
    AkiraInputDirect_WriteHidData  = 0,
    AkiraInputDirect_WriteHidData2 = 1,
    AkiraInputDirect_SetReport     = 2,
    AkiraInputDirect_MethodCount   = 3,
} AkiraInputDirectMethod;

typedef struct {
    uint8_t bt_addr[6];
    uint8_t reserved[2];
} AkiraInputPadRef;

enum {
    AkiraInputDevice_Identified  = 1u << 0,
    AkiraInputDevice_Reporting   = 1u << 2,
    AkiraInputDevice_Active      = 1u << 3,
    AkiraInputDevice_Claimed     = 1u << 4,
};

typedef struct {
    uint8_t  bt_addr[6];
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  report_id;
    uint8_t  flags;
    uint8_t  reserved[4];
    uint64_t last_report_ns;
} AkiraInputDeviceInfo;

#define AKIRA_INPUT_MAX_LISTED_DEVICES  6

typedef struct {
    uint8_t              count;
    uint8_t              reserved[7];
    AkiraInputDeviceInfo devices[AKIRA_INPUT_MAX_LISTED_DEVICES];
} AkiraInputDeviceList;

#define AKIRA_INPUT_OUTPUT_MAX 560

typedef struct {
    uint32_t rc_open_connection;
    uint32_t rc_event_wait;
    uint32_t rc_open_out;
    uint32_t rc_codec;
    uint32_t rc_parameter;
    uint32_t event_type;
    uint32_t audio_handle;
    uint32_t codec;
    uint32_t pcm_channels;
    int32_t  pcm_sample_rate;
    uint32_t pcm_bits_per_sample;
    uint32_t reserved;
} AkiraInputAudioProbe;

typedef struct {
    uint16_t length;
    uint16_t stored;
    uint8_t  data[128];
    uint8_t  reserved[4];
} AkiraInputDescriptor;

typedef struct {
    uint8_t bt_addr[6];
    uint8_t count;
    uint8_t report_ids[5];
    uint8_t reserved[4];
} AkiraInputZeroRetransmit;

typedef struct {
    uint8_t bt_addr[6];
    uint8_t acquire;
    uint8_t reserved;
} AkiraInputExternalControl;


typedef struct {
    uint8_t bt_addr[6];
    uint8_t reserved[2];
} AkiraInputTriggerQuery;

enum {
    AkiraInputBackend_Unavailable = 0,
    AkiraInputBackend_Ready       = 1,
    AkiraInputBackend_Active      = 2,
    AkiraInputBackend_Failed      = 3,
};

enum {
    AkiraInputTrigger_Unknown = 0,
    AkiraInputTrigger_Digital = 1,
    AkiraInputTrigger_Analog  = 2,
};

typedef struct {
    uint32_t api_version;
    uint32_t mc_version;
    uint8_t  backend_state;
    uint8_t  subscriber_count;
    uint8_t  reserved0[2];
    uint32_t last_unknown_vid_pid;
    uint64_t reports_received;
    uint64_t claims_held;
    uint64_t last_error;

    uint64_t output_written;
    uint64_t output_failed;
} AkiraInputStatus;

typedef struct {
    uint64_t timestamp_ns;
    uint16_t l2_raw;
    uint16_t r2_raw;
    uint16_t raw_max;
    uint8_t  capability;
    uint8_t  reserved0;
    uint8_t  bt_addr[6];
    uint8_t  reserved[2];
} AkiraInputTriggerState;

#define AKIRA_INPUT_RAW_MAX 80

typedef struct {
    uint64_t timestamp_ns;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t descriptor_length;
    uint8_t  reserved0;
    uint8_t  report_id;
    uint8_t  length;
    uint8_t  reserved;
    uint8_t  bt_addr[6];
    uint8_t  data[AKIRA_INPUT_RAW_MAX];
} AkiraInputRawReport;

#ifdef __cplusplus
static_assert(sizeof(AkiraInputStatus) == 56, "AkiraInputStatus size changed - bump API version");
static_assert(sizeof(AkiraInputTriggerState) == 24, "AkiraInputTriggerState size changed - bump API version");
static_assert(sizeof(AkiraInputTriggerQuery) == 8, "AkiraInputTriggerQuery size changed - bump API version");
static_assert(sizeof(AkiraInputRawReport) == 104, "AkiraInputRawReport size changed - bump API version");
static_assert(sizeof(AkiraInputDeviceInfo) == 24, "AkiraInputDeviceInfo size changed - bump API version");
static_assert(sizeof(AkiraInputDeviceList) == 152, "AkiraInputDeviceList size changed - bump API version");
static_assert(sizeof(AkiraInputPadRef) == 8, "AkiraInputPadRef size changed - bump API version");
static_assert(sizeof(AkiraInputAudioProbe) == 48, "AkiraInputAudioProbe size changed - bump API version");
static_assert(sizeof(AkiraInputDescriptor) == 136, "AkiraInputDescriptor size changed - bump API version");
static_assert(sizeof(AkiraInputExternalControl) == 8, "AkiraInputExternalControl size changed - bump API version");
static_assert(sizeof(AkiraInputZeroRetransmit) == 16, "AkiraInputZeroRetransmit size changed - bump API version");
static_assert(sizeof(AkiraInputDirectProbe) == 8, "AkiraInputDirectProbe size changed - bump API version");
static_assert(sizeof(AkiraInputDirectResult) == 8, "AkiraInputDirectResult size changed - bump API version");
#else
_Static_assert(sizeof(AkiraInputStatus) == 56, "AkiraInputStatus size changed - bump API version");
_Static_assert(sizeof(AkiraInputTriggerState) == 24, "AkiraInputTriggerState size changed - bump API version");
_Static_assert(sizeof(AkiraInputTriggerQuery) == 8, "AkiraInputTriggerQuery size changed - bump API version");
_Static_assert(sizeof(AkiraInputRawReport) == 104, "AkiraInputRawReport size changed - bump API version");
_Static_assert(sizeof(AkiraInputAudioProbe) == 48, "AkiraInputAudioProbe size changed - bump API version");
_Static_assert(sizeof(AkiraInputDescriptor) == 136, "AkiraInputDescriptor size changed - bump API version");
_Static_assert(sizeof(AkiraInputExternalControl) == 8, "AkiraInputExternalControl size changed - bump API version");
_Static_assert(sizeof(AkiraInputZeroRetransmit) == 16, "AkiraInputZeroRetransmit size changed - bump API version");
#endif

#ifdef __cplusplus
}
#endif

#endif /* AKIRA_INPUT_IPC_H */
