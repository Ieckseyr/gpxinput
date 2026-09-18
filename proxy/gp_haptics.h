// haptics
#ifndef GP_HAPTICS_H
#define GP_HAPTICS_H

#include <windows.h>
#include <stdint.h>

#include "gp_rdr2_state.h"



#define GP_WEAPON_SLOTS 8






typedef struct GpWeaponProfile {
    uint32_t hash;         
    float    shotGain;     
    int      shotEnvMs;    
    float    shotBodyKick; 
    float    aimTrig;      
    float    aimBody;      
    float    aimWobble;    
    char     name[24];     
} GpWeaponProfile;

struct GpHapticsSettings {
    



    BOOL  enable;

    
    

    BOOL  shotFromTrigger;
    float triggerPressThresh;  
    




    float triggerReleaseHyst;
    

    int   triggerRefractoryMs;

    




    BOOL  shotFromRumble;

    float shotRiseThresh;      
    float shotGain;            
    int   shotEnvMs;           
    int   shotRefractoryMs;    
    int   shotSide;            
    float shotBodyKick;        
    





    float rideShotBoost;

    
    BOOL  rideEnable;
    float ridePeakThresh;      
    float rideGain;            
    float rideTrigGain;        
    int   rideMinPeriodMs;     
    int   rideMaxPeriodMs;     
    float ridePeriodTol;       
    int   rideHoldMs;          

    
    

    float trigToBody;

    
    

    BOOL  useGameState;

    
    float aimBreathHz;

    


    float aimTriggerLevel;

    
    int   aimHoldMs;

    

    float aimRampMs;
    float aimRampGain;

    
    float tickGain;
    int   tickEnvMs;

    
    float drawGain;
    int   drawEnvMs;

    GpWeaponProfile weapon[GP_WEAPON_SLOTS];
    int             weaponCount;
};


struct GpHapticsOut {
    BYTE leftMotor;
    BYTE rightMotor;
    BYTE leftTrigger;
    BYTE rightTrigger;
};


void GpDefaultHapticsSettings(GpHapticsSettings* s);


void GpApplyHapticsSettings(const GpHapticsSettings& s);



void GpOnGameFrame(uint32_t controller, DWORD tick, BYTE bodyL, BYTE bodyR);







void GpOnPadInput(uint32_t controller, DWORD now, BYTE leftTrigger, BYTE rightTrigger);



void GpOnGameState(uint32_t controller, DWORD now, BOOL valid, const GpRdr2State* st);



void GpTickHaptics(uint32_t controller, DWORD now, BOOL hasGame,
                   BYTE baseL, BYTE baseR, BYTE baseTrigL, BYTE baseTrigR,
                   GpHapticsOut* out);



BOOL GpHapticsActive(uint32_t controller);


typedef struct GpHapticsStatus {
    uint8_t  shotActive;
    uint16_t shotRemainMs;
    uint8_t  rideActive;
    uint16_t ridePeriodMs;
    uint8_t  rideAmp;        
    uint8_t  stateValid;     
    uint8_t  aiming;         
    uint8_t  menuActive;     
    uint8_t  armed;          
    uint32_t weaponGroup;    
} GpHapticsStatus;

void GpHapticsGetStatus(uint32_t controller, DWORD now, GpHapticsStatus* out);


void GpResetHaptics(void);

#endif 
