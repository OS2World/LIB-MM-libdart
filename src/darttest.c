#define INCL_KBD
#define INCL_DOS
#include <os2.h>

#include <stdio.h>
#include <string.h>

#include "dart.h"

#define BUF_SIZE    1024

BYTE abBuf[ BUF_SIZE ];
int iBufIndex = 0;
int iBufLen = 0;

HMMIO   hmmio;
int     switch_sign = FALSE;

ULONG APIENTRY dartCallback ( PVOID pCBData, PVOID Buffer, ULONG BufferSize )
{
  PBYTE pbBuffer = Buffer;
  LONG  len;

  while( BufferSize > 0 )
  {
    if( iBufIndex >= iBufLen )
    {
        iBufLen = mmioRead( hmmio, abBuf, BUF_SIZE );
        if( iBufLen == 0 )
            break;
        iBufIndex = 0;
    }

    len = iBufLen - iBufIndex;
    if( len > BufferSize )
        len = BufferSize;
    memcpy( pbBuffer, &abBuf[ iBufIndex ], len );
    iBufIndex += len;
    pbBuffer += len;
    BufferSize -= len;
  }

  if (switch_sign)
  {
    char *sample = (char *)Buffer;
    char *lastsample = pbBuffer;
    while (sample < lastsample)
    {
      sample++;
      *sample ^= 0x80;
      sample++;
    } /* endwhile */
  } /* endif */

  return pbBuffer - ( PBYTE )Buffer;
}

int read_key(void)
{
    static ULONG time = 0;
    KBDKEYINFO Char;

    KbdCharIn(&Char, IO_NOWAIT, 0);

    if (time == Char.time)
        return 0;

    time = Char.time;

    return Char.chChar;
}

int main( int argc, char **argv )
{
    MMIOINFO        mmioInfo;
    MMAUDIOHEADER   mmAudioHeader;
    LONG            lBytesRead;
    int             key;
    CHAR            szPDDName[ 256 ] = "";
    APIRET          rc;

    if( argc < 2 )
    {
        fprintf( stderr, "Specify WAVE filename\r\n" );

        return 0;
    }

   /* Open the audio file.
    */
   memset( &mmioInfo, '\0', sizeof( MMIOINFO ));
   mmioInfo.fccIOProc = mmioFOURCC( 'W', 'A', 'V', 'E' );
   hmmio = mmioOpen( argv[ 1 ], &mmioInfo, MMIO_READ | MMIO_DENYNONE );

   if( !hmmio )
   {
      fprintf( stderr, "Unable to open wave file\r\n" );

      return 0;
   }

   /* Get the audio file header.
    */
   mmioGetHeader( hmmio,
                  &mmAudioHeader,
                  sizeof( MMAUDIOHEADER ),
                  &lBytesRead,
                  0,
                  0);

    OSLibGetAudioPDDName( szPDDName );
    printf("Available channels = %ld, PDD Name = %s\n", dartChNum(), szPDDName );

    rc = dartInit( 0,
                   mmAudioHeader.mmXWAVHeader.WAVEHeader.usBitsPerSample,
                   mmAudioHeader.mmXWAVHeader.WAVEHeader.ulSamplesPerSec,
                   mmAudioHeader.mmXWAVHeader.WAVEHeader.usFormatTag,
                   mmAudioHeader.mmXWAVHeader.WAVEHeader.usChannels,
                   2, 0, FALSE, dartCallback, NULL );

    printf("Silence = %02x\n", DART.bSilence );

    dartSetVolume( MCI_SET_AUDIO_ALL, 50 );
    dartSetSoundState( MCI_SET_AUDIO_ALL, TRUE );

    //DosSetPriority( PRTYS_THREAD, PRTYC_TIMECRITICAL, PRTYD_MAXIMUM, 0 );
    dartPlay();
    //DosSetPriority( PRTYS_THREAD, PRTYC_REGULAR, 0, 0 );

    while( DART.fPlaying )
    {
        key = read_key();

        if (key == 27)    /* ESC */
            break;

        if (key == 113)   /* q */
            dartStop();

        if (key == 119)   /* w */
            dartPlay();

        if (key == 101)   /* e */
            dartPause();

        if (key == 114)   /* r */
            dartResume();

        if (key == 99)    /* c */
            dartSetPos(34000L);

        fprintf(stdout, "Played: %ld %d      \r", dartGetPos(), key);
        fflush(stdout);
        DosSleep(200);
    }

    //getchar();
    dartClose();

    mmioClose( hmmio, 0 );

    return 0;
}
