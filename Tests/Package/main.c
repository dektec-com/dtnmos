// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# main.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - Parses an SDP with an installed dtnmos
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <dtnmos_sdp.h>
#include <stdbool.h>
#include <string.h>

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(void)
{
    const char* Text =
        "v=0\r\no=- 1 1 IN IP4 10.0.0.1\r\ns=x\r\nt=0 0\r\n"
        "m=audio 5004 RTP/AVP 97\r\nc=IN IP4 239.0.0.2/64\r\na=rtpmap:97 L24/48000/2\r\n";
    DtNmosSdp* Sdp = NULL;
    if (DtNmosSdp_Parse(Text, strlen(Text), &Sdp) != DTNMOS_OK)
    {
        return 1;
    }
    const DtNmosFlow* Flow = DtNmosSdp_Flow(Sdp, 0);
    const bool Ok = Flow != NULL && Flow->Media == DTNMOS_MEDIA_AUDIO &&
                    Flow->Format.Audio.Channels == 2;
    DtNmosSdp_Free(Sdp);
    return Ok ? 0 : 1;
}
