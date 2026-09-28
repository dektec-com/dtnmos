// SPDX-License-Identifier: BSD-3-Clause
//
// Parses an SDP with an installed dtnmos; exits with 0 when it reads the flow it
// describes.

#include <dtnmos/sdp.h>
#include <string.h>

int main(void)
{
  const char* text =
      "v=0\r\no=- 1 1 IN IP4 10.0.0.1\r\ns=x\r\nt=0 0\r\n"
      "m=audio 5004 RTP/AVP 97\r\nc=IN IP4 239.0.0.2/64\r\na=rtpmap:97 L24/48000/2\r\n";
  dtnmos_sdp* sdp = NULL;
  if (dtnmos_sdp_parse(text, strlen(text), &sdp, NULL) != DTNMOS_OK)
  {
    return 1;
  }
  const dtnmos_flow* flow = dtnmos_sdp_flow(sdp, 0);
  const int ok = flow != NULL && flow->media == DTNMOS_MEDIA_AUDIO &&
                 flow->format.audio.channels == 2;
  dtnmos_sdp_free(sdp);
  return ok ? 0 : 1;
}
