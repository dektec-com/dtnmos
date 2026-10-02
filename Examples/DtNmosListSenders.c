// #*#*#*#*#*#*#*#*#*#*#*#*# DtNmosListSenders.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - Example: lists the senders and receivers an NMOS registry holds
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Lists the senders an NMOS registry knows, and with --receivers its receivers too. The
// registry is the one at --registry, or one found on the network with DNS-SD. Prints a
// line for each: its ID, media, label and transport, and for a receiver the sender it is
// connected to. --sdp also downloads and prints the SDP of each sender that has one.
//
//     senders of http://192.168.1.5:8010:
//       9dfb9312-...  video  "dtnmos example sender"  urn:x-nmos:transport:rtp.mcast
//     receivers:
//       a3b1ccff-...  video  "dtnmos example receiver"  from 9dfb9312-... (active)
//
// Needs dtnmos built with libcurl. Exits with 0 when the registry answered, 2 when no
// registry was found, and 1 when a request failed.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <string.h>

#include "Common/ExampleCommon.h"

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption Options[] = {
    {"--registry", true, "The base URL of the registry, e.g. http://registry:8010"},
    {"--receivers", false, "Lists the receivers too"},
    {"--sdp", false, "Prints the SDP of each sender"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ListReceivers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Prints a line for each receiver of the registry. Returns the program's exit code.
//
static int ListReceivers(DtNmosQuery* Query)
{
    DtNmosReceiverList* List = NULL;
    const DtNmosResult Result = DtNmosQuery_Receivers(Query, &List);
    if (Result != DTNMOS_OK)
    {
        return Example_Failed("DtNmosQuery_Receivers", Result);
    }
    printf("receivers:\n");
    for (size_t i = 0; i < DtNmosReceiverList_Count(List); i++)
    {
        const DtNmosReceiverInfo* Receiver = DtNmosReceiverList_At(List, i);
        printf("  %s  %s  \"%s\"", Receiver->Id.Text, DtNmosMedia_Name(Receiver->Media),
               Receiver->Label);
        if (Receiver->SenderId.Text[0] != '\0')
        {
            printf("  from %s%s", Receiver->SenderId.Text,
                   Receiver->Active ? " (active)" : "");
        }
        printf("\n");
    }
    DtNmosReceiverList_Free(List);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ListSenders -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Prints a line for each sender of the registry at Url, and with PrintSdp its SDP.
// Returns the program's exit code.
//
// The SDP is downloaded into a buffer of 8 KB, which is ample. For a longer SDP,
// DtNmosQuery_SenderManifest returns DTNMOS_E_BUFFER_TOO_SMALL and the size it needs.
//
static int ListSenders(DtNmosQuery* Query, const char* Url, bool PrintSdp)
{
    DtNmosSenderList* List = NULL;
    const DtNmosResult Result = DtNmosQuery_Senders(Query, &List);
    if (Result != DTNMOS_OK)
    {
        return Example_Failed("DtNmosQuery_Senders", Result);
    }
    printf("senders of %s:\n", Url);
    for (size_t i = 0; i < DtNmosSenderList_Count(List); i++)
    {
        const DtNmosSenderInfo* Sender = DtNmosSenderList_At(List, i);
        printf("  %s  %s  \"%s\"  %s\n", Sender->Id.Text, DtNmosMedia_Name(Sender->Media),
               Sender->Label, Sender->Transport);
        if (!PrintSdp || Sender->ManifestHref[0] == '\0')
        {
            continue;
        }
        char Sdp[8192];
        size_t Size = sizeof(Sdp);
        const DtNmosResult Fetched =
            DtNmosQuery_SenderManifest(Query, Sender, Sdp, &Size);
        if (Fetched == DTNMOS_OK)
        {
            printf("%s\n", Sdp);
        }
        else
        {
            Example_Failed("DtNmosQuery_SenderManifest", Fetched);
        }
    }
    DtNmosSenderList_Free(List);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main(int Argc, char** Argv)
{
    if (!Example_CheckArguments(Argc, Argv,
                                "Lists the senders and receivers an NMOS registry holds.",
                                Options, (int)(sizeof(Options) / sizeof(Options[0]))))
    {
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
    int Exit = ListSenders(Query, Url, Example_HasFlag(Argc, Argv, "--sdp"));
    if (Exit == EXAMPLE_OK && Example_HasFlag(Argc, Argv, "--receivers"))
    {
        Exit = ListReceivers(Query);
    }
    DtNmosQuery_Freep(&Query);
    return Exit;
}
