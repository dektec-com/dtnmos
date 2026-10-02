// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtNmosConnect.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Example: connects a receiver of an NMOS registry to a sender, as a controller
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Connects a receiver to a sender, as an NMOS controller does: the receiver starts
// receiving what the sender sends. Both are given by ID or label, and found in the
// registry at --registry, or in one found on the network with DNS-SD. The program asks
// the receiver's node, through its Connection API (IS-05), to activate the receiver with
// the sender's SDP. --disconnect disconnects the receiver instead.
//
//     connected receiver a3b1ccff-... ("dtnmos example receiver") to sender 9dfb9312-...
//     ("dtnmos example sender")
//
// Needs dtnmos built with libcurl. Exits with 0 when the node took the activation, 2
// when no registry was found, and 1 when a request failed or the node refused.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <string.h>

#include "Common/ExampleCommon.h"

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption Options[] = {
    {"--registry", true, "The base URL of the registry, e.g. http://registry:8010"},
    {"--receiver", true, "The ID or label of the receiver"},
    {"--sender", true, "The ID or label of the sender to connect it to"},
    {"--disconnect", false, "Disconnects the receiver"},
    {"--sdp", false, "Prints the SDP the receiver was given"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Connects Receiver to Sender, prints what was connected and, with PrintSdp, the SDP
// the receiver was given. Returns the program's exit code.
//
static int Connect(DtNmosQuery* Query, const char* Receiver, const char* Sender,
                   bool PrintSdp)
{
    DtNmosConnection* Connection = NULL;
    const DtNmosResult Result = DtNmosQuery_Connect(Query, Receiver, Sender, &Connection);
    if (Result != DTNMOS_OK)
    {
        return Example_Failed("DtNmosQuery_Connect", Result);
    }
    const DtNmosReceiverInfo* Connected = DtNmosConnection_Receiver(Connection);
    const DtNmosSenderInfo* From = DtNmosConnection_Sender(Connection);
    printf("connected receiver %s (\"%s\") to sender %s (\"%s\")\n", Connected->Id.Text,
           Connected->Label, From->Id.Text, From->Label);
    if (PrintSdp)
    {
        printf("%s\n", DtNmosConnection_Sdp(Connection));
    }
    DtNmosConnection_Free(Connection);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Disconnect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Disconnects Receiver and prints it. Returns the program's exit code.
//
static int Disconnect(DtNmosQuery* Query, const char* Receiver)
{
    DtNmosReceiverList* Disconnected = NULL;
    const DtNmosResult Result = DtNmosQuery_Disconnect(Query, Receiver, &Disconnected);
    if (Result != DTNMOS_OK)
    {
        return Example_Failed("DtNmosQuery_Disconnect", Result);
    }
    const DtNmosReceiverInfo* Info = DtNmosReceiverList_At(Disconnected, 0);
    printf("disconnected receiver %s (\"%s\")\n", Info->Id.Text, Info->Label);
    DtNmosReceiverList_Free(Disconnected);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    if (!Example_CheckArguments(Argc, Argv,
                                "Connects a receiver of an NMOS registry to a sender, as "
                                "a controller of IS-05 does.",
                                Options, (int)(sizeof(Options) / sizeof(Options[0]))))
    {
        return EXAMPLE_FAILED;
    }
    const char* Receiver = Example_Value(Argc, Argv, "--receiver");
    const char* Sender = Example_Value(Argc, Argv, "--sender");
    const bool Disconnecting = Example_HasFlag(Argc, Argv, "--disconnect");
    if (Receiver == NULL || (Sender == NULL && !Disconnecting))
    {
        printf("Give --receiver, and --sender or --disconnect\n");
        return EXAMPLE_FAILED;
    }
    char Url[512];
    const char* Given = Example_Value(Argc, Argv, "--registry");
    if (Given != NULL)
    {
        snprintf(Url, sizeof(Url), "%s", Given);
    }
    else
    {
        const int Found = Example_FindRegistry(DTNMOS_SERVICE_QUERY, Url, sizeof(Url));
        if (Found != EXAMPLE_OK)
        {
            return Found;
        }
    }

    DtNmosQueryConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.RegistryUrl = Url;
    Config.Http = DtNmos_CurlHttp;
    Config.Log = Example_Log;
    DtNmosQuery* Query = DtNmosQuery_Alloc();
    DtNmosResult Result = Query == NULL ? DTNMOS_E_NO_MEMORY : DTNMOS_OK;
    if (Result == DTNMOS_OK)
    {
        Result = DtNmosQuery_Open(Query, &Config);
    }
    if (Result != DTNMOS_OK)
    {
        DtNmosQuery_Freep(&Query);
        return Example_Failed("DtNmosQuery_Open", Result);
    }
    const int Exit = Disconnecting ? Disconnect(Query, Receiver)
                                   : Connect(Query, Receiver, Sender,
                                             Example_HasFlag(Argc, Argv, "--sdp"));
    DtNmosQuery_Freep(&Query);
    return Exit;
}
