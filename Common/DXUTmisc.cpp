#include "d3dUtils.h"
#include "DXUTmisc.h"


namespace DXUT {

CDXUTTimer::CDXUTTimer() :
    m_bPaused(true),
    m_llTicksPerSec{},
    m_llStopStamp{},
    m_llLastStubStamp{},
    m_llBaseStamp{}
{
    // Use QueryPerformanceFrequency to get the frequency of the counter
    LARGE_INTEGER qwTicksPerSec = {};
    QueryPerformanceFrequency( &qwTicksPerSec );
    m_llTicksPerSec = qwTicksPerSec.QuadPart;

    Reset();
}

void CDXUTTimer::Reset()
{
    LONGLONG now;
    QueryPerformanceCounter((PLARGE_INTEGER)&now);
    m_llBaseStamp = m_llLastStubStamp = now;
    m_llStopStamp = 0;
    m_bPaused = FALSE;
}

void CDXUTTimer::Resume()
{
    if( m_bPaused ) {
        // Get the current time
        LONGLONG now;
        QueryPerformanceCounter((PLARGE_INTEGER)&now);

        m_llBaseStamp += now - m_llStopStamp;
        m_llLastStubStamp = m_llBaseStamp;
        m_llStopStamp = 0;
        m_bPaused = FALSE;
    }
}

void CDXUTTimer::Stop()
{
    if( !m_bPaused )
    {
        // Get the current time
        LONGLONG now;
        QueryPerformanceCounter((PLARGE_INTEGER)&now);
        m_llStopStamp = now;
        m_bPaused = TRUE;
    }
}
bool CDXUTTimer::IsPaused() const
{ return m_bPaused; }

double CDXUTTimer::GetTime() const
{
    double secs;
    if(m_bPaused) {
        secs = (m_llStopStamp - m_llBaseStamp) / (double) m_llTicksPerSec;
    } else {
        LONGLONG now;
        QueryPerformanceCounter((PLARGE_INTEGER)&now);
        secs = (now - m_llBaseStamp) / (double)m_llTicksPerSec;
    }

    if(secs < 0.0)
        secs = 0.0;

    return secs;
}


//--------------------------------------------------------------------------------------
double CDXUTTimer::GetElapsedTime() const
{
    double secs;
    if(m_bPaused) {
        secs = (m_llStopStamp - m_llLastStubStamp) / (double)m_llTicksPerSec;
    } else {
        LONGLONG now;
        QueryPerformanceCounter((PLARGE_INTEGER)&now);
        secs = (now - m_llLastStubStamp) / (double)m_llTicksPerSec;
    }

    // Clamp the timer to non-negative values to ensure the timer is accurate.
    // fElapsedTime can be outside this range if processor goes into a 
    // power save mode or we somehow get shuffled to another processor.  
    // However, the main thread should call SetThreadAffinityMask to ensure that 
    // we don't get shuffled to another processor.  Other worker threads should NOT call 
    // SetThreadAffinityMask, but use a shared copy of the timer data gathered from 
    // the main thread.
    if(secs < 0.0)
        secs = 0.0;

    return secs;
}

void CDXUTTimer::Tick() {
    if(!m_bPaused)
        QueryPerformanceCounter((PLARGE_INTEGER)&m_llLastStubStamp);
}

//--------------------------------------------------------------------------------------
// Limit the current thread to one processor (the current one). This ensures that timing code 
// runs on only one processor, and will not suffer any ill effects from power management.
// See "Game Timing and Multicore Processors" for more details
//--------------------------------------------------------------------------------------
void CDXUTTimer::LimitThreadAffinityToCurrentProc()
{
    HANDLE hCurrentProcess = GetCurrentProcess();

    // Get the processor affinity mask for this process
    DWORD_PTR dwProcessAffinityMask = 0;
    DWORD_PTR dwSystemAffinityMask = 0;

    if( GetProcessAffinityMask( hCurrentProcess, &dwProcessAffinityMask, &dwSystemAffinityMask ) != 0 &&
        dwProcessAffinityMask )
    {
        // Find the lowest processor that our process is allows to run against
        DWORD_PTR dwAffinityMask = ( dwProcessAffinityMask & ( ( ~dwProcessAffinityMask ) + 1 ) );

        // Set this as the processor that our thread must always run against
        // This must be a subset of the process affinity mask
        HANDLE hCurrentThread = GetCurrentThread();
        if( INVALID_HANDLE_VALUE != hCurrentThread )
        {
            SetThreadAffinityMask( hCurrentThread, dwAffinityMask );
            CloseHandle( hCurrentThread );
        }
    }

    CloseHandle( hCurrentProcess );
}

constexpr int DXUT_GAMEPAD_TRIGGER_THRESHOLD = 30;

typedef DWORD ( WINAPI* LPXINPUTGETSTATE )( DWORD dwUserIndex, XINPUT_STATE* pState );
typedef DWORD ( WINAPI* LPXINPUTSETSTATE )( DWORD dwUserIndex, XINPUT_VIBRATION* pVibration );
typedef DWORD ( WINAPI* LPXINPUTGETCAPABILITIES )( DWORD dwUserIndex, DWORD dwFlags,
                                                   XINPUT_CAPABILITIES* pCapabilities );
typedef void ( WINAPI* LPXINPUTENABLE )( BOOL bEnable );

//--------------------------------------------------------------------------------------
// Does extra processing on XInput data to make it slightly more convenient to use
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT DXUTGetGamepadState( DWORD dwPort, DXUT_GAMEPAD* pGamePad, bool bThumbstickDeadZone,
                             bool bSnapThumbstickToCardinals )
{
    if( dwPort >= DXUT_MAX_CONTROLLERS || !pGamePad )
        return E_FAIL;

    static LPXINPUTGETSTATE s_pXInputGetState = nullptr;
    static LPXINPUTGETCAPABILITIES s_pXInputGetCapabilities = nullptr;
    if( !s_pXInputGetState || !s_pXInputGetCapabilities )
    {
        HINSTANCE hInst = LoadLibraryEx( XINPUT_DLL, nullptr, 0x00000800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */ );
        if( hInst )
        {
            s_pXInputGetState = reinterpret_cast<LPXINPUTGETSTATE>( reinterpret_cast<void*>( GetProcAddress( hInst, "XInputGetState" ) ) );
            s_pXInputGetCapabilities = reinterpret_cast<LPXINPUTGETCAPABILITIES>( reinterpret_cast<void*>( GetProcAddress( hInst, "XInputGetCapabilities" ) ) );
        }
    }
    if( !s_pXInputGetState )
        return E_FAIL;

    XINPUT_STATE InputState;
    DWORD dwResult = s_pXInputGetState( dwPort, &InputState );

    // Track insertion and removals
    BOOL bWasConnected = pGamePad->bConnected;
    pGamePad->bConnected = ( dwResult == ERROR_SUCCESS );
    pGamePad->bRemoved = ( bWasConnected && !pGamePad->bConnected );
    pGamePad->bInserted = ( !bWasConnected && pGamePad->bConnected );

    // Don't update rest of the state if not connected
    if( !pGamePad->bConnected )
        return S_OK;

    // Store the capabilities of the device
    if( pGamePad->bInserted )
    {
        ZeroMemory( pGamePad, sizeof( DXUT_GAMEPAD ) );
        pGamePad->bConnected = true;
        pGamePad->bInserted = true;
        if( s_pXInputGetCapabilities )
            s_pXInputGetCapabilities( dwPort, XINPUT_DEVTYPE_GAMEPAD, &pGamePad->caps );
    }

    // Copy gamepad to local structure (assumes that XINPUT_GAMEPAD at the front in CONTROLER_STATE)
    memcpy( pGamePad, &InputState.Gamepad, sizeof( XINPUT_GAMEPAD ) );

    if( bSnapThumbstickToCardinals )
    {
        // Apply deadzone to each axis independantly to slightly snap to up/down/left/right
        if( pGamePad->sThumbLX < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE &&
            pGamePad->sThumbLX > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE )
            pGamePad->sThumbLX = 0;
        if( pGamePad->sThumbLY < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE &&
            pGamePad->sThumbLY > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE )
            pGamePad->sThumbLY = 0;
        if( pGamePad->sThumbRX < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE &&
            pGamePad->sThumbRX > -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE )
            pGamePad->sThumbRX = 0;
        if( pGamePad->sThumbRY < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE &&
            pGamePad->sThumbRY > -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE )
            pGamePad->sThumbRY = 0;
    }
    else if( bThumbstickDeadZone )
    {
        // Apply deadzone if centered
        if( ( pGamePad->sThumbLX < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE &&
              pGamePad->sThumbLX > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ) &&
            ( pGamePad->sThumbLY < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE &&
              pGamePad->sThumbLY > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ) )
        {
            pGamePad->sThumbLX = 0;
            pGamePad->sThumbLY = 0;
        }
        if( ( pGamePad->sThumbRX < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE &&
              pGamePad->sThumbRX > -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE ) &&
            ( pGamePad->sThumbRY < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE &&
              pGamePad->sThumbRY > -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE ) )
        {
            pGamePad->sThumbRX = 0;
            pGamePad->sThumbRY = 0;
        }
    }

    // Convert [-1,+1] range
    pGamePad->fThumbLX = pGamePad->sThumbLX / 32767.0f;
    pGamePad->fThumbLY = pGamePad->sThumbLY / 32767.0f;
    pGamePad->fThumbRX = pGamePad->sThumbRX / 32767.0f;
    pGamePad->fThumbRY = pGamePad->sThumbRY / 32767.0f;

    // Get the boolean buttons that have been pressed since the last call. 
    // Each button is represented by one bit.
    pGamePad->wPressedButtons = ( pGamePad->wLastButtons ^ pGamePad->wButtons ) & pGamePad->wButtons;
    pGamePad->wLastButtons = pGamePad->wButtons;

    // Figure out if the left trigger has been pressed or released
    bool bPressed = ( pGamePad->bLeftTrigger > DXUT_GAMEPAD_TRIGGER_THRESHOLD );
    pGamePad->bPressedLeftTrigger = ( bPressed ) ? !pGamePad->bLastLeftTrigger : false;
    pGamePad->bLastLeftTrigger = bPressed;

    // Figure out if the right trigger has been pressed or released
    bPressed = ( pGamePad->bRightTrigger > DXUT_GAMEPAD_TRIGGER_THRESHOLD );
    pGamePad->bPressedRightTrigger = ( bPressed ) ? !pGamePad->bLastRightTrigger : false;
    pGamePad->bLastRightTrigger = bPressed;

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Don't pause the game or deactive the window without first stopping rumble otherwise 
// the controller will continue to rumble
//--------------------------------------------------------------------------------------
void DXUTEnableXInput( _In_ bool bEnable )
{
    static LPXINPUTENABLE s_pXInputEnable = nullptr;
    if( !s_pXInputEnable )
    {
        HINSTANCE hInst = LoadLibraryEx( XINPUT_DLL, nullptr, 0x00000800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */ );
        if( hInst )
            s_pXInputEnable = reinterpret_cast<LPXINPUTENABLE>( reinterpret_cast<void*>( GetProcAddress( hInst, "XInputEnable" ) ) );
    }

    if( s_pXInputEnable )
        s_pXInputEnable( bEnable );
}


//--------------------------------------------------------------------------------------
// Don't pause the game or deactive the window without first stopping rumble otherwise 
// the controller will continue to rumble
//--------------------------------------------------------------------------------------
HRESULT DXUTStopRumbleOnAllControllers()
{
    static LPXINPUTSETSTATE s_pXInputSetState = nullptr;
    if( !s_pXInputSetState )
    {
        HINSTANCE hInst = LoadLibraryEx( XINPUT_DLL, nullptr, 0x00000800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */ );
        if( hInst )
            s_pXInputSetState = reinterpret_cast<LPXINPUTSETSTATE>( reinterpret_cast<void*>( GetProcAddress( hInst, "XInputSetState" ) ) );
    }
    if( !s_pXInputSetState )
        return E_FAIL;

    XINPUT_VIBRATION vibration;
    vibration.wLeftMotorSpeed = 0;
    vibration.wRightMotorSpeed = 0;
    for( int iUserIndex = 0; iUserIndex < DXUT_MAX_CONTROLLERS; iUserIndex++ )
        s_pXInputSetState( iUserIndex, &vibration );

    return S_OK;
}
}
