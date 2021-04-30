#pragma once
#include <Xinput.h>

namespace DXUT {
//--------------------------------------------------------------------------------------
// Performs timer operations
// Use DXUTGetGlobalTimer() to get the global instance
//--------------------------------------------------------------------------------------
class CDXUTTimer
{
public:
    CDXUTTimer();

    void            Reset(); // resets the timer
    void            Resume(); // starts the timer
    void            Stop();  // stop (or pause) the timer
    bool            IsPaused() const; // returns true if timer stopped
    double          GetTime() const; // get the current time after the lastest Reset() or Resume() call.
    void            Tick();
    double          GetElapsedTime() const; // get the time that elapsed after the lastest Tick() call.

    // Limit the current thread to one processor (the current one). This ensures that timing code runs
    // on only one processor, and will not suffer any ill effects from power management.
    void            LimitThreadAffinityToCurrentProc();
protected:
    bool m_bPaused;
    LONGLONG m_llTicksPerSec;

    LONGLONG m_llStopStamp;
    LONGLONG m_llLastStubStamp;
    LONGLONG m_llBaseStamp;
};

//--------------------------------------------------------------------------------------
// XInput helper state/function
// This performs extra processing on XInput gamepad data to make it slightly more convenient to use
// 
// Example usage:
//
//      DXUT_GAMEPAD gamepad[4];
//      for( DWORD iPort=0; iPort<DXUT_MAX_CONTROLLERS; iPort++ )
//          DXUTGetGamepadState( iPort, gamepad[iPort] );
//
//--------------------------------------------------------------------------------------
constexpr int DXUT_MAX_CONTROLLERS =  4;  // XInput handles up to 4 controllers 

struct DXUT_GAMEPAD
{
    // From XINPUT_GAMEPAD
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;

    // Device properties
    XINPUT_CAPABILITIES caps;
    bool bConnected; // If the controller is currently connected
    bool bInserted;  // If the controller was inserted this frame
    bool bRemoved;   // If the controller was removed this frame

    // Thumb stick values converted to range [-1,+1]
    float fThumbRX;
    float fThumbRY;
    float fThumbLX;
    float fThumbLY;

    // Records which buttons were pressed this frame.
    // These are only set on the first frame that the button is pressed
    WORD wPressedButtons;
    bool bPressedLeftTrigger;
    bool bPressedRightTrigger;

    // Last state of the buttons
    WORD wLastButtons;
    bool bLastLeftTrigger;
    bool bLastRightTrigger;
};

HRESULT DXUTGetGamepadState( _In_ DWORD dwPort, _In_ DXUT_GAMEPAD* pGamePad, _In_ bool bThumbstickDeadZone = true,
                             _In_ bool bSnapThumbstickToCardinals = true );
HRESULT DXUTStopRumbleOnAllControllers();
void DXUTEnableXInput( _In_ bool bEnable );

};