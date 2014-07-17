/*
    Direct Audio Interface library for OS/2
    Copyright (C) 2007 by KO Myung-Hun <komh@chollian.net>
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
        KO Myung-Hun <komh@chollian.net> 2007/02/11
            - Removed the following variables in DARTSTRUCT
                BOOL  fResampleFlg;
                BOOL  fNormalizeFlg;
                ULONG ulLastTimeStamp;
                ULONG ulCurrTimeStamp;

            - Removed the following macros
                DART_CH_ALL      0
                DART_CH_LEFT     1
                DART_CH_RIGHT    2

            - Instead, use MCI_SET_AUDIO_* macros.

        KO Myung-Hun <komh@chollian.net> 2007/02/16
            - Changed fStopped to fPlaying in DARTSTRUCT

        KO Myung-Hun <komh@chollian.net> 2007/02/25
            - Removed the following variables in DARTSTRUCT
                BOOL               fPaused
                BOOL               fWaitStreamEnd
                ULONG              ulBytesPlayed
                ULONG              ulSeekPosition
                BOOL               fShareable
                ULONG              ulCurrVolume
                BOOL               fSamplesPlayed
                USHORT             usDeviceID
                PMCI_MIX_BUFFER    pMixBuffers
                MCI_MIXSETUP_PARMS MixSetupParms
                MCI_BUFFER_PARMS   BufferParms
                PFNDICB            pfndicb

        KO Myung-Hun <komh@chollian.net> 2008/03/30
            - Added callback data as a parameter to dartInit()
*/

#ifndef __DART_H__
#define __DART_H__

#define INCL_OS2MM
#include <os2.h>
#include <os2me.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ULONG ( APIENTRY FNDICB )( PVOID, PVOID, ULONG );
typedef FNDICB *PFNDICB;

#pragma pack(1)
typedef struct DARTSTRUCT
{
    BOOL               fPlaying;
    ULONG              ulBufferSize;
    ULONG              ulNumBuffers;
    BYTE               bSilence;
    CHAR               szErrorCode[ CCHMAXPATH ];
} DARTSTRUCT, *PDARTSTRUCT;

#pragma pack()

extern DARTSTRUCT DART;

APIRET APIENTRY dartInit( USHORT usDeviceIndex, ULONG ulBitsPerSample,
                           ULONG ulSamplingRate, ULONG ulDataFormat, ULONG ulChannels,
                           ULONG ulNumBuffers, ULONG ulBufferSize, BOOL fShareable,
                           PFNDICB pfndicb, PVOID pCBData );
APIRET APIENTRY dartClose( VOID );
APIRET APIENTRY dartPlay( VOID );
APIRET APIENTRY dartStop( VOID );
APIRET APIENTRY dartPause( VOID );
APIRET APIENTRY dartResume( VOID );
APIRET APIENTRY dartGetPos( VOID );
APIRET APIENTRY dartSetPos( ULONG ulNewPos );
APIRET APIENTRY dartError( APIRET rc );
APIRET APIENTRY dartSetSoundState( ULONG ulCh, BOOL fState );
APIRET APIENTRY dartSetVolume( ULONG ulCh, USHORT usVol );
APIRET APIENTRY dartGetVolume( VOID );
APIRET APIENTRY dartChNum( VOID );
APIRET APIENTRY dartClearBuffer( VOID );
APIRET APIENTRY OSLibGetAudioPDDName ( PSZ pszPDDName );

#ifdef __cpluslus
}
#endif

#endif
