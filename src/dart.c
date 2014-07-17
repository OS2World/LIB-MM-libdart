/*
    Direct Audio Interface library for OS/2
    Copyright (C) 2007-2008 by KO Myung-Hun <komh@chollian.net>
    Copyright (C) by Alex Strelnikov
    Copyright (C) 1998 by Andrew Zabolotny <bit@eltech.ru>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Changes :
        KO Myung-Hun <komh@chollian.net> 2007/02/03
            - if ulNumBuffer in dartInit() is 0, it is set to DART_MIN_BUFFERS
              not the suggested thing by the mixer device.

        KO Myung-Hun <komh@chollian.net> 2007/02/11
            - Use MCI_SET_AUDIO_* macros instead of DART_CH_* macros.

        KO Myung-Hun <komh@chollian.net> 2007/02/16
            - Prevent dartPlay() and dartStop() from executing many times.
            - Added buffer filling thread to reserve enough stack because
              DART callback stack is too small. (by Dmitry Froloff)
            - Use fPlaying instead of fStopped.

        KO Myung-Hun <komh@chollian.net> 2007/02/25
            - Added the following variables as static storage instead of
              as global storage in DARTSTRUCT.

                BOOL               m_fWaitStreamEnd
                BOOL               m_fShareable
                ULONG              m_ulCurrVolume
                USHORT             m_usDeviceID
                PMCI_MIX_BUFFER    m_pMixBuffers
                MCI_MIXSETUP_PARMS m_MixSetupParms
                MCI_BUFFER_PARMS   m_BufferParms
                PFNDICB            m_pfndicb

        KO Myung-Hun <komh@chollian.net> 2007/04/09
            - Changed output stream of dartError() from stdout to stderr.
              ( by Dmitry Froloff )

        KO Myung-Hun <komh@chollian.net> 2007/04/24
            - Use PRTYD_MAXIMUM instead of +31 for DosSetPriority().

        KO Myung-Hun <komh@chollian.net> 2007/06/12
            - Added MCI_WAIT flag to some mciSendCommand() calls.
            - Fixed a invalid command to release the exclusive use of device
              instance.

        KO Myung-Hun <komh@chollian.net> 2007/12/25
            - Do not change the priority of dartFillThread() to TIMECRITICAL.
              SNAP seems not to like this.

        KO Myung-Hun <komh@chollian.net> 2008/03/30
            - Added callback data as a parameter to dartInit().

        KO Myung-Hun <komh@chollian.net> 2008/08/11
            - Load MDM.DLL dynamically, so no need to link with mmpm2.

        KO Myung-Hun <komh@chollian.net> 2008/09/27
            - Include process.h for _beginthread() declaration in case of
              Open Watcom.
*/

#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_OS2MM
#include <os2.h>
#include <os2me.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __WATCOMC__
#include <process.h>
#endif

#include "dart.h"

// Currently(Warp4 FixPak #15), MDM.DLL allows only one load per process.
// Otherwise, DosLoadModule() return ERROR_INIT_ROUTINE_FAILED.
// So we should load it only once, and let system free it automatically
// at finish.
#define LET_SYSTEM_FREE_MODULE

// DART need 2 buffers at least to play.
#define DART_MIN_BUFFERS     2

DARTSTRUCT DART = { 0 };

static BOOL  m_fDartInited = FALSE;

static HEV   m_hevFill = NULLHANDLE;
static HEV   m_hevFillDone = NULLHANDLE;
static TID   m_tidFillThread = 0;
static PVOID m_pFillArg = NULL;

static BOOL               m_fWaitStreamEnd = FALSE;
static BOOL               m_fShareable = FALSE;
static ULONG              m_ulCurrVolume = 0;
static USHORT             m_usDeviceID = 0;
static PMCI_MIX_BUFFER    m_pMixBuffers = NULL;
static MCI_MIXSETUP_PARMS m_MixSetupParms = { 0 , };
static MCI_BUFFER_PARMS   m_BufferParms = { 0, };
static PFNDICB            m_pfndicb = NULL;
static PVOID              m_pCBData = NULL;

static HMODULE m_hmodMDM = NULLHANDLE;

#ifdef __IBMC__
#define DECLARE_PFN( ret, callconv, name, arg ) ret ( * callconv name )arg
#else
#define DECLARE_PFN( ret, callconv, name, arg ) ret ( callconv * name )arg
#endif

static DECLARE_PFN( ULONG, APIENTRY, m_pfnmciSendCommand, ( USHORT, USHORT, ULONG, PVOID, USHORT ));
static DECLARE_PFN( ULONG, APIENTRY, m_pfnmciGetErrorString, ( ULONG, PSZ, USHORT ));

static VOID freeMDM( VOID )
{
    DosFreeModule( m_hmodMDM );
    m_hmodMDM = NULLHANDLE;
}

static BOOL loadMDM( VOID )
{
    UCHAR szFailedName[ 256 ];

    if( m_hmodMDM )
        return TRUE;

    if( DosLoadModule( szFailedName, sizeof( szFailedName ), "MDM.DLL", &m_hmodMDM ))
        return FALSE;

    if( DosQueryProcAddr( m_hmodMDM, 1, NULL, ( PFN * )&m_pfnmciSendCommand ))
        goto exit_error;

    if( DosQueryProcAddr( m_hmodMDM, 3, NULL, ( PFN * )&m_pfnmciGetErrorString ))
        goto exit_error;

    return TRUE;

exit_error:
    // In case of this, MDM.DLL is not a MMOS2 DLL.
    freeMDM();

    return FALSE;
}

APIRET APIENTRY dartError( APIRET rc )
{
    if(( USHORT )rc )
    {
    #ifndef LET_SYSTEM_FREE_MODULE
        BOOL fNeedFree = FALSE;

        if( m_hmodMDM == NULLHANDLE )
        {
    #endif
            if( !loadMDM())
            {
                fprintf( stderr, "\nDART error:Cannot load MDM.DLL\n");

                return -1;
            }
    #ifndef LET_SYSTEM_FREE_MODULE

            fNeedFree = TRUE;
        }
    #endif
        m_pfnmciGetErrorString( rc,
                                ( PSZ )DART.szErrorCode,
                                sizeof( DART.szErrorCode ));

        fprintf( stderr, "\nDART error(%lx):%s\n", rc, DART.szErrorCode );

    #ifndef LET_SYSTEM_FREE_MODULE
        if( fNeedFree )
            freeMDM();
    #endif

        return rc;
    }

    DART.szErrorCode[ 0 ] = 0;

    return 0;
}


APIRET APIENTRY dartStop(void)
{
    MCI_GENERIC_PARMS GenericParms;
    ULONG             rc;

    if( !m_fDartInited )
        return -1;

    if( !DART.fPlaying )
        return 0;

    memset( &GenericParms, 0, sizeof( GenericParms ));

    GenericParms.hwndCallback = 0;

    rc = m_pfnmciSendCommand( m_usDeviceID,
                              MCI_STOP,
                              MCI_WAIT,
                              ( PVOID )&GenericParms,
                              0 );
    if( dartError( rc ))
        return rc;

    DART.fPlaying = FALSE;

    DosPostEventSem( m_hevFill );
    while( DosWaitThread( &m_tidFillThread, DCWW_WAIT ) == ERROR_INTERRUPT );
    DosCloseEventSem( m_hevFill );

    DosPostEventSem( m_hevFillDone);
    DosCloseEventSem( m_hevFillDone );

    m_pFillArg = NULL;

    return 0;
}

APIRET APIENTRY dartClearBuffer( VOID )
{
    int i;

    if( !m_fDartInited )
        return -1;

    for( i = 0; i < DART.ulNumBuffers; i++)
       memset( m_pMixBuffers[ i ].pBuffer, DART.bSilence, DART.ulBufferSize );

    return 0;
}

static ULONG dartFillBuffer( PMCI_MIX_BUFFER pBuffer )
{
    ULONG ulWritten = 0;

    memset( pBuffer->pBuffer, DART.bSilence, DART.ulBufferSize );

    if( m_pfndicb )
        ulWritten = m_pfndicb( m_pCBData, pBuffer->pBuffer, DART.ulBufferSize );

    if( ulWritten < DART.ulBufferSize )
    {
        pBuffer->ulFlags = MIX_BUFFER_EOS;
        m_fWaitStreamEnd = TRUE;
    }
    else
        pBuffer->ulFlags = 0;

    return ulWritten;
}

APIRET APIENTRY dartFreeBuffers( VOID )
{
    APIRET  rc;

    if( !m_fDartInited )
        return -1;

    if( m_pMixBuffers == NULL )
        return 0;

    rc = m_pfnmciSendCommand( m_usDeviceID,
                              MCI_BUFFER,
                              MCI_WAIT | MCI_DEALLOCATE_MEMORY,
                              ( PVOID )&m_BufferParms, 0 );
    if( dartError( rc ))
        return rc;

    if( m_pMixBuffers )
        free( m_pMixBuffers );

    m_pMixBuffers = NULL;

    return 0;
}

static void dartFillThread( void *arg )
{
    ULONG ulPost;

    //DosSetPriority( PRTYS_THREAD, PRTYC_TIMECRITICAL, PRTYD_MAXIMUM, 0 );

    for(;;)
    {
        while( DosWaitEventSem( m_hevFill, SEM_INDEFINITE_WAIT ) == ERROR_INTERRUPT );
        DosResetEventSem( m_hevFill, &ulPost );

        if( !DART.fPlaying )
            break;

        // Transfer buffer to DART
        dartFillBuffer( m_pFillArg );

        DosPostEventSem( m_hevFillDone );
    }
}

#define DART_FILL_BUFFERS( pBuffer ) \
{\
    static ULONG ulPost = 0;\
\
    m_pFillArg = pBuffer;\
    DosPostEventSem( m_hevFill );\
\
    while( DosWaitEventSem( m_hevFillDone, SEM_INDEFINITE_WAIT ) == ERROR_INTERRUPT );\
    DosResetEventSem( m_hevFillDone, &ulPost );\
}

LONG APIENTRY MixHandler( ULONG ulStatus, PMCI_MIX_BUFFER pBuffer, ULONG ulFlags )
{
    switch( ulFlags )
    {
        case MIX_STREAM_ERROR | MIX_WRITE_COMPLETE:
            // on error, fill next buffer and continue
        case MIX_WRITE_COMPLETE:
        {
            // If this is the last buffer, stop
            if( pBuffer->ulFlags & MIX_BUFFER_EOS)
                dartStop();
            else if( DART.fPlaying && !m_fWaitStreamEnd )
            {
                DART_FILL_BUFFERS( pBuffer );

                m_MixSetupParms.pmixWrite( m_MixSetupParms.ulMixHandle, m_pFillArg, 1 );
            }
            break;
        }
    }

    return TRUE;
}

APIRET APIENTRY dartChNum( VOID )
{
    ULONG               ulDartFlags;
    ULONG               ulChannels = 0;
    MCI_AMP_OPEN_PARMS  AmpOpenParms;
    MCI_GENERIC_PARMS   GenericParms;
    MCI_MIXSETUP_PARMS  MixSetupParms;

#ifndef LET_SYSTEM_FREE_MODULE
    BOOL                fNeedFree = FALSE;

    if( m_hmodMDM == NULLHANDLE )
    {
#endif
        if( !loadMDM())
            return ulChannels;

#ifndef LET_SYSTEM_FREE_MODULE
        fNeedFree = TRUE;
    }
#endif

    memset( &AmpOpenParms, 0, sizeof( MCI_AMP_OPEN_PARMS ));
    ulDartFlags = MCI_WAIT | MCI_OPEN_TYPE_ID | MCI_OPEN_SHAREABLE;
    AmpOpenParms.usDeviceID = 0;
    AmpOpenParms.pszDeviceType = (PSZ)( MCI_DEVTYPE_AUDIO_AMPMIX |
                                        ( 0 << 16 ));
    if( m_pfnmciSendCommand( 0, MCI_OPEN, ulDartFlags, ( PVOID )&AmpOpenParms, 0 ))
        goto exit;

    ulChannels = 6; // first, try 6 channels
    memset( &MixSetupParms, 0, sizeof( MCI_MIXSETUP_PARMS ));
    MixSetupParms.ulBitsPerSample = 16;
    MixSetupParms.ulSamplesPerSec = 48000;
    MixSetupParms.ulFormatTag = MCI_WAVE_FORMAT_PCM;
    MixSetupParms.ulChannels = ulChannels;
    MixSetupParms.ulFormatMode = MCI_PLAY;
    MixSetupParms.ulDeviceType = MCI_DEVTYPE_WAVEFORM_AUDIO;
    MixSetupParms.pmixEvent = NULL;
    if( m_pfnmciSendCommand( AmpOpenParms.usDeviceID,
                             MCI_MIXSETUP, MCI_WAIT | MCI_MIXSETUP_INIT,
                             ( PVOID )&MixSetupParms, 0 ))
    {
        ulChannels = 4; // failed. try 4 channels
        MixSetupParms.ulChannels = ulChannels;
        if( m_pfnmciSendCommand( AmpOpenParms.usDeviceID,
                                 MCI_MIXSETUP, MCI_WAIT | MCI_MIXSETUP_INIT,
                                 ( PVOID )&MixSetupParms, 0 ))
            ulChannels = 2; // failed again...so, drivers support only 2 channels
    }

    m_pfnmciSendCommand( AmpOpenParms.usDeviceID,
                         MCI_CLOSE, MCI_WAIT,
                         ( PVOID )&GenericParms, 0 );
exit:
#ifndef LET_SYSTEM_FREE_MODULE
    if( fNeedFree )
        freeMDM();
#endif

    return ulChannels;
}

APIRET APIENTRY dartInit( USHORT usDeviceIndex, ULONG ulBitsPerSample, ULONG ulSamplingRate,
                          ULONG ulDataFormat, ULONG ulChannels, ULONG ulNumBuffers,
                          ULONG ulBufferSize, BOOL fShareable, PFNDICB pfndicb, PVOID pCBData )
{
    APIRET              rc;
    ULONG               ulDartFlags;
    MCI_AMP_OPEN_PARMS  AmpOpenParms;
    MCI_GENERIC_PARMS   GenericParms;

    m_fDartInited = FALSE;

    if( !loadMDM())
        return -1;

    memset( &DART, 0, sizeof( DARTSTRUCT ));

    DART.bSilence = ( ulBitsPerSample == BPS_16 ) ? 0x00 : 0x80;

    m_fShareable = fShareable;
    if( m_fShareable )
        ulDartFlags = MCI_WAIT | MCI_OPEN_TYPE_ID | MCI_OPEN_SHAREABLE;
    else
        ulDartFlags = MCI_WAIT | MCI_OPEN_TYPE_ID;

    memset( &AmpOpenParms, 0, sizeof( MCI_AMP_OPEN_PARMS ));

    AmpOpenParms.usDeviceID = 0;
    AmpOpenParms.pszDeviceType = (PSZ)( MCI_DEVTYPE_AUDIO_AMPMIX |
                                        (( ULONG )usDeviceIndex << 16 ));
    rc = m_pfnmciSendCommand( 0,
                              MCI_OPEN,
                              ulDartFlags,
                              ( PVOID )&AmpOpenParms,
                              0 );
    if( dartError( rc ))
        return rc;

    m_usDeviceID = AmpOpenParms.usDeviceID;

    if( !m_fShareable )
    {
        // Grab exclusive rights to device instance (NOT entire device)
        GenericParms.hwndCallback = 0;
        rc = m_pfnmciSendCommand( m_usDeviceID,
                                  MCI_ACQUIREDEVICE,
                                  MCI_WAIT | MCI_EXCLUSIVE_INSTANCE,
                                  ( PVOID )&GenericParms,
                                  0 );
        if( dartError( rc ))
            goto exit_release;
    }

    // Setup the mixer for playback of wave data
    memset( &m_MixSetupParms, 0, sizeof( MCI_MIXSETUP_PARMS ));

    m_MixSetupParms.ulBitsPerSample = ulBitsPerSample;
    m_MixSetupParms.ulSamplesPerSec = ulSamplingRate;
    m_MixSetupParms.ulFormatTag = ulDataFormat;
    m_MixSetupParms.ulChannels = ulChannels;
    m_MixSetupParms.ulFormatMode = MCI_PLAY;
    m_MixSetupParms.ulDeviceType = MCI_DEVTYPE_WAVEFORM_AUDIO;
    m_MixSetupParms.pmixEvent = ( MIXEREVENT * )MixHandler;

    rc = m_pfnmciSendCommand( m_usDeviceID,
                              MCI_MIXSETUP,
                              MCI_WAIT | MCI_MIXSETUP_INIT,
                              ( PVOID )&m_MixSetupParms,
                              0 );

    if( dartError( rc ))
        goto exit_close;

    if( ulNumBuffers < DART_MIN_BUFFERS )
        ulNumBuffers = DART_MIN_BUFFERS;

    // Use the suggested buffer number and size provide by the mixer device if 0
    if( ulNumBuffers == 0 )
        ulNumBuffers = m_MixSetupParms.ulNumBuffers;

    if( ulBufferSize == 0 )
        ulBufferSize = m_MixSetupParms.ulBufferSize;

    // Allocate mixer buffers
    m_pMixBuffers = ( MCI_MIX_BUFFER * )malloc( sizeof( MCI_MIX_BUFFER ) * ulNumBuffers );

    // Set up the BufferParms data structure and allocate device buffers
    // from the Amp-Mixer
    m_BufferParms.ulStructLength = sizeof( MCI_BUFFER_PARMS );
    m_BufferParms.ulNumBuffers = ulNumBuffers;
    m_BufferParms.ulBufferSize = ulBufferSize;
    m_BufferParms.pBufList = m_pMixBuffers;

    rc = m_pfnmciSendCommand( m_usDeviceID,
                              MCI_BUFFER,
                              MCI_WAIT | MCI_ALLOCATE_MEMORY,
                              ( PVOID )&m_BufferParms,
                              0 );
    if( dartError( rc ))
        goto exit_deallocate;

    // The mixer possibly changed these values
    DART.ulNumBuffers = m_BufferParms.ulNumBuffers;
    DART.ulBufferSize = m_BufferParms.ulBufferSize;

    m_pfndicb = pfndicb;
    m_pCBData = pCBData;

    m_fDartInited = TRUE;

    return 0;

exit_deallocate :
    free( m_pMixBuffers );
    m_pMixBuffers = NULL;

exit_release :
    if( !m_fShareable )
    {
        // Release exclusive rights to device instance (NOT entire device)
        m_pfnmciSendCommand( m_usDeviceID,
                             MCI_RELEASEDEVICE,
                             MCI_WAIT | MCI_RETURN_RESOURCE,
                             ( PVOID )&GenericParms,
                             0 );
    }

exit_close :
    m_pfnmciSendCommand( m_usDeviceID,
                         MCI_CLOSE,
                         MCI_WAIT,
                         ( PVOID )&GenericParms,
                         0 );

#ifndef LET_SYSTEM_FREE_MODULE
    freeMDM();
#endif

    return rc;
}


APIRET APIENTRY dartClose( VOID )
{
    MCI_GENERIC_PARMS   GenericParms;
    APIRET              rc;

    if( !m_fDartInited )
        return -1;

    dartStop();
    dartFreeBuffers();

    GenericParms.hwndCallback = 0;

    if( !m_fShareable )
    {
        // Release exclusive rights to device instance (NOT entire device)
        rc = m_pfnmciSendCommand( m_usDeviceID,
                                  MCI_RELEASEDEVICE,
                                  MCI_WAIT | MCI_RETURN_RESOURCE,
                                  ( PVOID )&GenericParms,
                                  0 );
        if( dartError( rc ))
            return rc;
    }

    rc = m_pfnmciSendCommand( m_usDeviceID,
                              MCI_CLOSE,
                              MCI_WAIT,
                              ( PVOID )&GenericParms,
                              0 );
    if( dartError( rc ))
        return rc;

    //freeMDM();

    m_fDartInited = FALSE;

    return 0;
}

APIRET APIENTRY dartPlay( VOID )
{
    int   i;
    ULONG rc;

    if( !m_fDartInited )
        return -1;

    if( DART.fPlaying )
        return 0;

    m_fWaitStreamEnd = FALSE;
    DART.fPlaying = TRUE;

    DosCreateEventSem( NULL, &m_hevFill, 0, FALSE );
    DosCreateEventSem( NULL, &m_hevFillDone, 0, FALSE );
    m_tidFillThread = _beginthread( dartFillThread, NULL, 256 * 1024, NULL );

    for( i = 0; i < DART.ulNumBuffers; i++ )
    {
        m_pMixBuffers[ i ].ulBufferLength = DART.ulBufferSize;
        m_pMixBuffers[ i ].ulFlags = 0;
    }

    for( i = 0; i < DART_MIN_BUFFERS; i++ )
    {
        DART_FILL_BUFFERS( &m_pMixBuffers[ i ]);

        if( m_fWaitStreamEnd )
            break;
    }

    if( i < DART_MIN_BUFFERS )
        i++;

    rc = m_MixSetupParms.pmixWrite( m_MixSetupParms.ulMixHandle,
                                    m_pMixBuffers, i );
    if( dartError( rc ))
    {
        dartStop();

        return rc;
    }

    return 0;
}

APIRET APIENTRY dartPause( VOID )
{
    MCI_GENERIC_PARMS GenericParms;
    ULONG             rc;

    if( !m_fDartInited )
        return -1;

    m_ulCurrVolume = dartGetVolume();
    rc = m_pfnmciSendCommand( m_usDeviceID,
                              MCI_PAUSE,
                              MCI_WAIT,
                              ( PVOID )&GenericParms,
                              0 );
    if( dartError( rc ))
        return rc;

    return 0;
}


APIRET APIENTRY dartResume( VOID )
{
    MCI_GENERIC_PARMS GenericParms;
    ULONG             rc;

    if( !m_fDartInited )
        return -1;


    rc = m_pfnmciSendCommand( m_usDeviceID,
                              MCI_RESUME,
                              MCI_WAIT,
                              ( PVOID )&GenericParms,
                              0 );
    if( dartError( rc ))
        return rc;

    // setting volume of channels separately can be failed.
    if( LOUSHORT( m_ulCurrVolume ) == HIUSHORT( m_ulCurrVolume ))
        dartSetVolume( MCI_SET_AUDIO_ALL, LOUSHORT( m_ulCurrVolume ));
    else
    {
        dartSetVolume( MCI_SET_AUDIO_LEFT, LOUSHORT( m_ulCurrVolume ));
        dartSetVolume( MCI_SET_AUDIO_RIGHT, HIUSHORT( m_ulCurrVolume ));
    }

    return 0;
}

APIRET APIENTRY dartGetPos( VOID )
{
    MCI_STATUS_PARMS StatusParms;
    ULONG            rc;

    if( !m_fDartInited )
        return 0;

    memset( &StatusParms, 0, sizeof( MCI_STATUS_PARMS ));
    StatusParms.ulItem = MCI_STATUS_POSITION;

    rc = m_pfnmciSendCommand( m_usDeviceID,
                              MCI_STATUS,
                              MCI_WAIT | MCI_STATUS_ITEM,
                              ( PVOID )&StatusParms,
                              0 );
    if( dartError( rc ))
        return 0;

    return StatusParms.ulReturn;
}


APIRET APIENTRY dartSetPos( ULONG ulNewPos )
{
    APIRET          rc;
    MCI_SEEK_PARMS  SeekParms;

    if( !m_fDartInited )
        return -1;

    SeekParms.hwndCallback = 0;
    SeekParms.ulTo = ulNewPos;

    rc = m_pfnmciSendCommand( m_usDeviceID,
                              MCI_SEEK,
                              MCI_WAIT | MCI_TO,
                              ( PVOID )&SeekParms,
                              0 );
    if( dartError( rc ))
        return rc;

    return 0;
}


APIRET APIENTRY dartSetSoundState( ULONG ulCh, BOOL fState)
{
    MCI_SET_PARMS SetParms;
    USHORT        usSt;
    ULONG         rc;

    if( !m_fDartInited )
        return -1;

    if( fState)
        usSt = MCI_SET_ON;
    else
        usSt = MCI_SET_OFF;

    SetParms.ulAudio = ulCh;

    rc = m_pfnmciSendCommand( m_usDeviceID,
                              MCI_SET,
                              MCI_WAIT | MCI_SET_AUDIO | usSt,
                              ( PVOID)&SetParms, 0 );
    if( dartError( rc ))
        return rc;

    return 0;
}


APIRET APIENTRY dartSetVolume( ULONG ulCh, USHORT usVol)
{
    MCI_SET_PARMS SetParms;
    ULONG         rc;

    if( !m_fDartInited )
        return -1;

    SetParms.ulLevel = usVol;
    SetParms.ulAudio = ulCh;
    rc = m_pfnmciSendCommand( m_usDeviceID,
                              MCI_SET,
                              MCI_WAIT | MCI_SET_AUDIO |
                              MCI_SET_VOLUME,
                              ( PVOID )&SetParms, 0);
    if( dartError( rc ))
        return rc;

    return 0;
}


APIRET APIENTRY dartGetVolume( VOID )
{
    MCI_STATUS_PARMS StatusParms;
    ULONG            rc;

    if( !m_fDartInited )
        return 0;

    memset(&StatusParms, 0, sizeof( MCI_STATUS_PARMS ));
    StatusParms.ulItem = MCI_STATUS_VOLUME;

    rc = m_pfnmciSendCommand( m_usDeviceID,
                              MCI_STATUS,
                              MCI_WAIT | MCI_STATUS_ITEM,
                              ( PVOID )&StatusParms,
                              0 );
    if( dartError( rc ))
        return 0;

    return StatusParms.ulReturn;
}

/******************************************************************************/
// OS/2 32-bit program to query the Physical Device Driver name
// for the default MMPM/2 WaveAudio device.  Joe Nord 10-Mar-1999
/******************************************************************************/
APIRET APIENTRY OSLibGetAudioPDDName( PSZ pszPDDName )
{
    ULONG                   ulRC;
    CHAR                    szAmpMix[9] = "AMPMIX01";

    MCI_SYSINFO_PARMS       SysInfo;
    MCI_SYSINFO_LOGDEVICE   SysInfoParm;
    MCI_SYSINFO_QUERY_NAME  QueryNameParm;

#ifndef LET_SYSTEM_FREE_MODULE
    BOOL                    fNeedFree = FALSE;

    if( m_hmodMDM == NULLHANDLE )
    {
#endif
        if( !loadMDM())
            return -1;

#ifndef LET_SYSTEM_FREE_MODULE
        fNeedFree = TRUE;
    }
#endif

    memset( &SysInfo, '\0', sizeof( SysInfo ));
    memset( &SysInfoParm, '\0', sizeof( SysInfoParm ));
    memset( &QueryNameParm, '\0', sizeof( QueryNameParm ));

    SysInfo.ulItem       = MCI_SYSINFO_QUERY_NAMES;
    SysInfo.usDeviceType = MCI_DEVTYPE_WAVEFORM_AUDIO;
    SysInfo.pSysInfoParm = &QueryNameParm;

    strcpy( QueryNameParm.szLogicalName, szAmpMix );

    ulRC = m_pfnmciSendCommand( 0,
                                MCI_SYSINFO,
                                MCI_SYSINFO_ITEM | MCI_WAIT,
                                ( PVOID )&SysInfo,
                                0 );
    if( dartError( ulRC ))
        goto exit;

//   printf("Audio:\n install name [%s]\n logical name [%s]\n alias [%s]\n type num: %i\n ord num: %i\n",
//          QueryNameParm.szInstallName,  /*  Device install name. */
//          QueryNameParm.szLogicalName,  /*  Logical device name. */
//          QueryNameParm.szAliasName,    /*  Alias name. */
//          QueryNameParm.usDeviceType,   /*  Device type number. */
//          QueryNameParm.usDeviceOrd);   /*  Device type o */

    // Get PDD associated with our AmpMixer
    // Device name is in pSysInfoParm->szPDDName
    SysInfo.ulItem       = MCI_SYSINFO_QUERY_DRIVER;
    SysInfo.usDeviceType = MCI_DEVTYPE_WAVEFORM_AUDIO;
    SysInfo.pSysInfoParm = &SysInfoParm;

    strcpy( SysInfoParm.szInstallName, QueryNameParm.szInstallName );

    ulRC = m_pfnmciSendCommand( 0,
                                MCI_SYSINFO,
                                MCI_SYSINFO_ITEM | MCI_WAIT,
                                ( PVOID )&SysInfo,
                                0 );
    if( dartError( ulRC ))
        goto exit;

//    strcpy( pszPDDName, SysInfoParm.szPDDName );
    strcpy ( pszPDDName, SysInfoParm.szProductInfo );
//    printf("Audio:\n product info [%s]\n\n",SysInfoParm.szProductInfo);
//    printf("Audio:\n inst name [%s]\n version [%s]\n MCD drv [%s]\n VSD drv [%s]\n res name: [%s]\n",
//           SysInfoParm.szInstallName,
//           SysInfoParm.szVersionNumber,
//           SysInfoParm.szMCDDriver,
//           SysInfoParm.szVSDDriver,
//           SysInfoParm.szResourceName);

exit:
#ifndef LET_SYSTEM_FREE_MODULE
    if( fNeedFree )
        freeMDM();
#endif

    return ulRC;
}

