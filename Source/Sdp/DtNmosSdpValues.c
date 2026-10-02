// #*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosSdpValues.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// dtnmos - The values ST 2110 lists for the text fields of a flow, as enums and back
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <string.h>

#include "NmosFlow.h"

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Tables -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Each table holds the spelling of each value of its enum, in the order of the enum, the
// first two, of _NONE and _OTHER, empty. The SDP's values are compared as they are
// written, case and all, as the standards spell them.
//

static const char* const AudioEncodings[] = {"", "", "L16", "L24", "AM824"};

static const char* const Colorimetries[] = {
    "",         "",         "BT601",       "BT709", "BT2020", "BT2100",
    "ST2065-1", "ST2065-3", "UNSPECIFIED", "XYZ",   "ALPHA"};

static const char* const PackingModes[] = {"", "", "2110GPM", "2110BPM"};

static const char* const Ranges[] = {"", "", "NARROW", "FULLPROTECT", "FULL"};

static const char* const Samplings[] = {"",
                                        "",
                                        "YCbCr-4:4:4",
                                        "YCbCr-4:2:2",
                                        "YCbCr-4:2:0",
                                        "CLYCbCr-4:4:4",
                                        "CLYCbCr-4:2:2",
                                        "CLYCbCr-4:2:0",
                                        "ICtCp-4:4:4",
                                        "ICtCp-4:2:2",
                                        "ICtCp-4:2:0",
                                        "RGB",
                                        "XYZ",
                                        "KEY"};

static const char* const Tcss[] = {"",           "",        "SDR",         "PQ",
                                   "HLG",        "LINEAR",  "BT2100LINPQ", "BT2100LINHLG",
                                   "ST2065-1",   "ST428-1", "DENSITY",     "ST2115LOGS3",
                                   "UNSPECIFIED"};

static const char* const TransmitterTypes[] = {"", "", "2110TPN", "2110TPNL", "2110TPW"};

#define NMOS_COUNT(Table) (sizeof(Table) / sizeof((Table)[0]))

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IndexOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The index of Text in Table, of Count spellings: 0, that of _NONE, for null or empty,
// and 1, that of _OTHER, for one it does not have.
//
static int IndexOf(const char* const* Table, size_t Count, const char* Text)
{
    if (Text == NULL || Text[0] == '\0')
    {
        return 0;
    }
    for (size_t i = 2; i < Count; ++i)
    {
        if (strcmp(Table[i], Text) == 0)
        {
            return (int)i;
        }
    }
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TextOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The spelling at Index in Table, of Count spellings, or "" for _NONE, _OTHER and one
// outside it.
//
static const char* TextOf(const char* const* Table, size_t Count, int Index)
{
    return Index > 1 && (size_t)Index < Count ? Table[Index] : "";
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAudioEncoding_FromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosAudioEncoding DtNmosAudioEncoding_FromText(const char* Text)
{
    return (DtNmosAudioEncoding)IndexOf(AudioEncodings, NMOS_COUNT(AudioEncodings), Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAudioEncoding_Text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosAudioEncoding_Text(DtNmosAudioEncoding AudioEncoding)
{
    return TextOf(AudioEncodings, NMOS_COUNT(AudioEncodings), (int)AudioEncoding);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosColorimetry_FromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosColorimetry DtNmosColorimetry_FromText(const char* Text)
{
    return (DtNmosColorimetry)IndexOf(Colorimetries, NMOS_COUNT(Colorimetries), Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosColorimetry_Text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosColorimetry_Text(DtNmosColorimetry Colorimetry)
{
    return TextOf(Colorimetries, NMOS_COUNT(Colorimetries), (int)Colorimetry);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosPackingMode_FromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosPackingMode DtNmosPackingMode_FromText(const char* Text)
{
    return (DtNmosPackingMode)IndexOf(PackingModes, NMOS_COUNT(PackingModes), Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosPackingMode_Text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosPackingMode_Text(DtNmosPackingMode PackingMode)
{
    return TextOf(PackingModes, NMOS_COUNT(PackingModes), (int)PackingMode);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRange_FromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosRange DtNmosRange_FromText(const char* Text)
{
    return (DtNmosRange)IndexOf(Ranges, NMOS_COUNT(Ranges), Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosRange_Text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosRange_Text(DtNmosRange Range)
{
    return TextOf(Ranges, NMOS_COUNT(Ranges), (int)Range);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSampling_FromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosSampling DtNmosSampling_FromText(const char* Text)
{
    return (DtNmosSampling)IndexOf(Samplings, NMOS_COUNT(Samplings), Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosSampling_Text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtNmosSampling_Text(DtNmosSampling Sampling)
{
    return TextOf(Samplings, NMOS_COUNT(Samplings), (int)Sampling);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosTcs_FromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosTcs DtNmosTcs_FromText(const char* Text)
{
    return (DtNmosTcs)IndexOf(Tcss, NMOS_COUNT(Tcss), Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosTcs_Text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosTcs_Text(DtNmosTcs Tcs)
{
    return TextOf(Tcss, NMOS_COUNT(Tcss), (int)Tcs);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosTransmitterType_FromText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtNmosTransmitterType DtNmosTransmitterType_FromText(const char* Text)
{
    return (DtNmosTransmitterType)IndexOf(TransmitterTypes, NMOS_COUNT(TransmitterTypes),
                                          Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosTransmitterType_Text -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtNmosTransmitterType_Text(DtNmosTransmitterType TransmitterType)
{
    return TextOf(TransmitterTypes, NMOS_COUNT(TransmitterTypes), (int)TransmitterType);
}
