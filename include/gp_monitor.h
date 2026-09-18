// monitor
#ifndef GP_MONITOR_H
#define GP_MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPMON_MAGIC   0x4D4F5031u   
#define GPMON_VERSION 2u
#define GPMON_MAX_CONTROLLERS 4u


#define GPMON_NAME L"Local\\GpXInput_v2_mon"


#define GPMON_HEARTBEAT_TIMEOUT_MS 1500u

typedef struct GpMonController {
    
    volatile uint32_t seq;

    uint32_t tickMs;      

    

    uint8_t  gameLM, gameRM, gameLT, gameRT;  
    uint8_t  outLM,  outRM,  outLT,  outRT;   
    uint8_t  padLT,  padRT;                   

    
    uint8_t  shotActive;      
    uint16_t shotRemainMs;    
    uint8_t  rideActive;      
    uint16_t ridePeriodMs;    
    uint8_t  rideAmp;         

    

    uint8_t  stateValid;
    uint8_t  aiming;
    uint16_t pad1_;
    uint32_t weaponGroup;

    uint8_t  hasGame;         
    uint8_t  padValid;        
    uint16_t pad0;            
    uint32_t outCount;        
} GpMonController;

typedef struct GpMonBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t writerHeartbeat; 
    uint32_t writerPid;
    uint32_t mode;            
    uint32_t hidCount;        
    uint32_t reserved;
    GpMonController ctl[GPMON_MAX_CONTROLLERS];
} GpMonBlock;

#ifdef __cplusplus
}  
#endif

#endif 
