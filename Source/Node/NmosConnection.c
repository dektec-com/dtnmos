// #*#*#*#*#*#*#*#*#*#*#*#*#* NmosConnection.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - The Connection API of the node (AMWA IS-05 v1.1)
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NmosJson.h"
#include "NmosNode.h"

// The port of RTP when nothing gives another (RFC 3551).
#define NMOS_RTP_PORT 5004

// The transport parameters of the one leg of RTP. An address is "auto", an address, or
// "" for null; a port is -1 for "auto". The active parameters hold no "auto".
typedef struct NmosLeg
{
    char SourceIp[DTNMOS_MAX_ADDRESS_SIZE];
    char DestinationIp[DTNMOS_MAX_ADDRESS_SIZE]; // of a sender
    char MulticastIp[DTNMOS_MAX_ADDRESS_SIZE];   // of a receiver
    char InterfaceIp[DTNMOS_MAX_ADDRESS_SIZE];   // of a receiver
    int SourcePort;                              // of a sender
    int DestinationPort;
    bool RtpEnabled;
} NmosLeg;

// The staged or active parameters of a sender or receiver, with the activation of IS-05:
// in the staged parameters the one that was asked for, in the active ones the last that
// took place. A scheduled activation is pending while the staged parameters have DueNs.
typedef struct NmosParameters
{
    bool MasterEnable;
    char PeerId[37]; // receiver_id of a sender, sender_id of a receiver; "" for null
    NmosLeg Transport;
    char* TransportFile;     // of a receiver: the SDP it was given, or null
    char Mode[32];           // of the activation, e.g. "activate_immediate"; "" for null
    char RequestedTime[32];  // of a scheduled activation; "" for null
    char ActivationTime[32]; // when it takes or took place; "" for null
    uint64_t DueNs;          // when a scheduled activation takes place, in TAI; or 0
} NmosParameters;

typedef struct NmosConnection
{
    NmosParameters Staged;
    NmosParameters Active;
    bool Applying; // A callback applies an activation; a PATCH meanwhile is refused
} NmosConnection;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static char* CopyText(const char* Text)
{
    if (Text == NULL)
    {
        return NULL;
    }
    const size_t Length = strlen(Text);
    char* Copy = malloc(Length + 1);
    if (Copy != NULL)
    {
        memcpy(Copy, Text, Length + 1);
    }
    return Copy;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsAuto -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether an address the node chooses is left to it: "auto", or null.
//
static bool IsAuto(const char* Address)
{
    return Address[0] == '\0' || strcmp(Address, "auto") == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyParameters -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes target a copy of source; returns false when out of memory, leaving target as it
// was.
//
static bool CopyParameters(NmosParameters* Target, const NmosParameters* Source)
{
    char* File = CopyText(Source->TransportFile);
    if (Source->TransportFile != NULL && File == NULL)
    {
        return false;
    }
    free(Target->TransportFile);
    *Target = *Source;
    Target->TransportFile = File;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeConnection -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FreeConnection(NmosConnection* c)
{
    if (c != NULL)
    {
        free(c->Staged.TransportFile);
        free(c->Active.TransportFile);
        free(c);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosConnection_InitSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosConnection_InitSender(NmosNodeSender* Sender)
{
    NmosConnection* c = calloc(1, sizeof(*c));
    if (c == NULL)
    {
        return DTNMOS_E_NO_MEMORY;
    }
    // A sender sends from its port where its element was told to, until a
    // controller moves it.
    NmosLeg* t = &c->Active.Transport;
    snprintf(t->SourceIp, sizeof(t->SourceIp), "%s", Sender->SourceIp);
    snprintf(Sender->ActiveSourceIp, sizeof(Sender->ActiveSourceIp), "%s",
             Sender->SourceIp);
    snprintf(t->DestinationIp, sizeof(t->DestinationIp), "%s",
             Sender->Flow.DestinationIp);
    t->SourcePort = Sender->Flow.DestinationPort;
    t->DestinationPort = Sender->Flow.DestinationPort;
    t->RtpEnabled = true;
    c->Active.MasterEnable = true;
    c->Staged = c->Active;
    Sender->Connection = c;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosConnection_ClearSender -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosConnection_ClearSender(NmosNodeSender* Sender)
{
    FreeConnection(Sender->Connection);
    Sender->Connection = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosConnection_InitReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosConnection_InitReceiver(NmosNodeReceiver* Receiver,
                                         const DtNmosReceiverConfig* Config)
{
    NmosConnection* c = calloc(1, sizeof(*c));
    if (c == NULL)
    {
        return DTNMOS_E_NO_MEMORY;
    }
    // A receiver receives what the program gave it until a controller connects it: by
    // default from any source, on its port, at the port of RTP.
    NmosLeg* t = &c->Active.Transport;
    snprintf(t->InterfaceIp, sizeof(t->InterfaceIp), "%s", Receiver->InterfaceIp);
    t->DestinationPort = NMOS_RTP_PORT;
    if (Config->Size >=
        offsetof(DtNmosReceiverConfig, DestinationPort) + sizeof(Config->DestinationPort))
    {
        snprintf(t->SourceIp, sizeof(t->SourceIp), "%s",
                 Config->SourceIp != NULL ? Config->SourceIp : "");
        snprintf(t->MulticastIp, sizeof(t->MulticastIp), "%s",
                 Config->MulticastIp != NULL ? Config->MulticastIp : "");
        if (Config->DestinationPort != 0)
        {
            t->DestinationPort = Config->DestinationPort;
        }
    }
    t->RtpEnabled = true;
    c->Active.MasterEnable = true;
    c->Staged = c->Active;
    Receiver->Connection = c;
    Receiver->MasterEnable = true;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosConnection_ClearReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosConnection_ClearReceiver(NmosNodeReceiver* Receiver)
{
    FreeConnection(Receiver->Connection);
    Receiver->Connection = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteTextOrNull -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes a text, e.g. an address, or null for "".
//
static void WriteTextOrNull(NmosBuffer* b, const char* Text)
{
    if (Text[0] == '\0')
    {
        DTNMOS_APPEND_LITERAL(b, "null");
    }
    else
    {
        NmosJson_WriteString(b, Text);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WritePort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void WritePort(NmosBuffer* b, int Port)
{
    if (Port < 0)
    {
        DTNMOS_APPEND_LITERAL(b, "\"auto\"");
    }
    else
    {
        NmosBuffer_Printf(b, "%d", Port);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteParameters -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the parameters p of a sender or receiver; WithActivation writes the activation
// of p, which staged parameters show in the answer to their PATCH and while it is
// scheduled.
//
static void WriteParameters(NmosBuffer* b, const NmosParameters* p, bool Sender,
                            bool WithActivation)
{
    NmosBuffer_Printf(b, "{\"%s\": ", Sender ? "receiver_id" : "sender_id");
    WriteTextOrNull(b, p->PeerId);
    NmosBuffer_Printf(b, ", \"master_enable\": %s, \"activation\": ",
                      p->MasterEnable ? "true" : "false");
    if (WithActivation && p->Mode[0] != '\0')
    {
        NmosBuffer_Printf(b, "{\"mode\": \"%s\", \"requested_time\": ", p->Mode);
        WriteTextOrNull(b, p->RequestedTime);
        DTNMOS_APPEND_LITERAL(b, ", \"activation_time\": ");
        WriteTextOrNull(b, p->ActivationTime);
        DTNMOS_APPEND_LITERAL(b, "}");
    }
    else
    {
        DTNMOS_APPEND_LITERAL(b, "{\"mode\": null, \"requested_time\": null, "
                                 "\"activation_time\": null}");
    }
    if (!Sender)
    {
        DTNMOS_APPEND_LITERAL(b, ", \"transport_file\": {\"data\": ");
        if (p->TransportFile != NULL)
        {
            NmosJson_WriteString(b, p->TransportFile);
            DTNMOS_APPEND_LITERAL(b, ", \"type\": \"application/sdp\"}");
        }
        else
        {
            DTNMOS_APPEND_LITERAL(b, "null, \"type\": null}");
        }
    }
    DTNMOS_APPEND_LITERAL(b, ", \"transport_params\": [{\"source_ip\": ");
    WriteTextOrNull(b, p->Transport.SourceIp);
    if (Sender)
    {
        DTNMOS_APPEND_LITERAL(b, ", \"destination_ip\": ");
        WriteTextOrNull(b, p->Transport.DestinationIp);
        DTNMOS_APPEND_LITERAL(b, ", \"source_port\": ");
        WritePort(b, p->Transport.SourcePort);
    }
    else
    {
        DTNMOS_APPEND_LITERAL(b, ", \"multicast_ip\": ");
        WriteTextOrNull(b, p->Transport.MulticastIp);
        DTNMOS_APPEND_LITERAL(b, ", \"interface_ip\": ");
        WriteTextOrNull(b, p->Transport.InterfaceIp);
    }
    DTNMOS_APPEND_LITERAL(b, ", \"destination_port\": ");
    WritePort(b, p->Transport.DestinationPort);
    NmosBuffer_Printf(b, ", \"rtp_enabled\": %s}]}",
                      p->Transport.RtpEnabled ? "true" : "false");
}

// The constraints of the one leg: each parameter of the leg, unconstrained.
static const char SenderConstraints[] =
    "[{\"source_ip\": {}, \"destination_ip\": {}, \"source_port\": {}, "
    "\"destination_port\": {}, \"rtp_enabled\": {}}]";
static const char ReceiverConstraints[] =
    "[{\"source_ip\": {}, \"multicast_ip\": {}, \"interface_ip\": {}, "
    "\"destination_port\": {}, \"rtp_enabled\": {}}]";

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AnswerText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void AnswerText(DtNmosHttpResponse* Response, const char* Text)
{
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    NmosBuffer_Append(&b, Text, strlen(Text));
    NmosNode_AnswerJson(Response, &b);
    NmosBuffer_Free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads an address: a string, or null into "". Returns false for another type.
//
static bool ReadAddress(const NmosJson* Value, char* Target, size_t Size)
{
    if (Value->Type == DTNMOS_JSON_NULL)
    {
        Target[0] = '\0';
        return true;
    }
    if (Value->Type != DTNMOS_JSON_STRING || Value->StringLength >= Size)
    {
        return false;
    }
    memcpy(Target, Value->String, Value->StringLength + 1);
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads a port: a whole number of 0 to 65535, or "auto" into -1.
//
static bool ReadPort(const NmosJson* Value, int* Port)
{
    if (Value->Type == DTNMOS_JSON_STRING && strcmp(Value->String, "auto") == 0)
    {
        *Port = -1;
        return true;
    }
    if (Value->Type != DTNMOS_JSON_NUMBER || Value->Number < 0 || Value->Number > 65535 ||
        (double)(int)Value->Number != Value->Number)
    {
        return false;
    }
    *Port = (int)Value->Number;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadBool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool ReadBool(const NmosJson* Value, bool* Target)
{
    if (Value->Type != DTNMOS_JSON_TRUE && Value->Type != DTNMOS_JSON_FALSE)
    {
        return false;
    }
    *Target = Value->Type == DTNMOS_JSON_TRUE;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadTime -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads a time of IS-05, "<seconds>:<nanoseconds>", into nanoseconds; returns false for
// another text, or one too far out to count in nanoseconds.
//
static bool ReadTime(const char* Text, uint64_t* Ns)
{
    uint64_t Seconds = 0;
    const char* p = Text;
    for (; *p >= '0' && *p <= '9'; ++p)
    {
        if (Seconds > 1000000000000u)
        {
            return false;
        }
        Seconds = Seconds * 10 + (uint64_t)(*p - '0');
    }
    if (p == Text || *p != ':')
    {
        return false;
    }
    const char* Fraction = ++p;
    uint64_t Nanoseconds = 0;
    for (; *p >= '0' && *p <= '9'; ++p)
    {
        Nanoseconds = Nanoseconds * 10 + (uint64_t)(*p - '0');
        if (Nanoseconds >= 1000000000u)
        {
            return false;
        }
    }
    if (p == Fraction || *p != '\0')
    {
        return false;
    }
    *Ns = Seconds * 1000000000u + Nanoseconds;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteTime -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes nanoseconds as a time of IS-05, "<seconds>:<nanoseconds>", into Text.
//
static void WriteTime(uint64_t Ns, char* Text, size_t Size)
{
    snprintf(Text, Size, "%llu:%llu", (unsigned long long)(Ns / 1000000000u),
             (unsigned long long)(Ns % 1000000000u));
}

// What a PATCH asks of the activation of the staged parameters.
#define NMOS_ACTIVATE_NONE 0
#define NMOS_ACTIVATE_NOW 1
#define NMOS_ACTIVATE_LATER 2

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadActivation -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the activation of a PATCH, of mode Name and Requested time, into Staged, and sets
// Activate to what it asks. A mode of null asks for none, which cancels a scheduled one;
// a scheduled one gets its time in TAI, a relative one from now. Returns a message on
// failure.
//
static const char* ReadActivation(const char* Name, const NmosJson* Requested,
                                  NmosParameters* Staged, int* Activate)
{
    *Activate = NMOS_ACTIVATE_NONE;
    Staged->Mode[0] = '\0';
    Staged->RequestedTime[0] = '\0';
    Staged->ActivationTime[0] = '\0';
    Staged->DueNs = 0;
    if (Name == NULL)
    {
        return NULL;
    }
    if (strcmp(Name, "activate_immediate") == 0)
    {
        *Activate = NMOS_ACTIVATE_NOW;
    }
    else if (strcmp(Name, "activate_scheduled_absolute") == 0 ||
             strcmp(Name, "activate_scheduled_relative") == 0)
    {
        const char* Time = NmosJson_Text(Requested);
        uint64_t Ns = 0;
        if (Time == NULL || !ReadTime(Time, &Ns) ||
            strlen(Time) >= sizeof(Staged->RequestedTime))
        {
            return "A scheduled activation has a requested_time of "
                   "\"<seconds>:<nanoseconds>\".";
        }
        const bool Relative = strcmp(Name, "activate_scheduled_relative") == 0;
        Staged->DueNs = Relative ? NmosOs_TaiNowNs() + Ns : Ns;
        // A time of 0 is that of the epoch, long past, and due at once like it.
        Staged->DueNs += Staged->DueNs == 0 ? 1 : 0;
        snprintf(Staged->RequestedTime, sizeof(Staged->RequestedTime), "%s", Time);
        WriteTime(Staged->DueNs, Staged->ActivationTime, sizeof(Staged->ActivationTime));
        *Activate = NMOS_ACTIVATE_LATER;
    }
    else
    {
        return "activation has a mode of IS-05: activate_immediate, "
               "activate_scheduled_absolute or activate_scheduled_relative, or null.";
    }
    snprintf(Staged->Mode, sizeof(Staged->Mode), "%s", Name);
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MergeLeg -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Merges the transport parameters of the one leg into t; returns a message on failure.
//
static const char* MergeLeg(const NmosJson* Value, bool Sender, NmosLeg* t)
{
    if (Value->Type != DTNMOS_JSON_ARRAY || Value->Count != 1 ||
        Value->Items[0].Type != DTNMOS_JSON_OBJECT)
    {
        return "transport_params holds the parameters of one leg.";
    }
    const NmosJson* Params = &Value->Items[0];
    for (size_t i = 0; i < Params->Count; ++i)
    {
        const char* Name = Params->Keys[i];
        const NmosJson* Member = &Params->Items[i];
        if (!Sender &&
            (strcmp(Name, "source_ip") == 0 || strcmp(Name, "multicast_ip") == 0) &&
            Member->Type == DTNMOS_JSON_STRING && strcmp(Member->String, "auto") == 0)
        {
            return "The source_ip and multicast_ip of a receiver are an address or null.";
        }
        bool Valid = false;
        if (strcmp(Name, "source_ip") == 0)
        {
            Valid = ReadAddress(Member, t->SourceIp, sizeof(t->SourceIp));
        }
        else if (Sender && strcmp(Name, "destination_ip") == 0)
        {
            Valid = ReadAddress(Member, t->DestinationIp, sizeof(t->DestinationIp));
        }
        else if (!Sender && strcmp(Name, "multicast_ip") == 0)
        {
            Valid = ReadAddress(Member, t->MulticastIp, sizeof(t->MulticastIp));
        }
        else if (!Sender && strcmp(Name, "interface_ip") == 0)
        {
            Valid = ReadAddress(Member, t->InterfaceIp, sizeof(t->InterfaceIp));
        }
        else if (Sender && strcmp(Name, "source_port") == 0)
        {
            Valid = ReadPort(Member, &t->SourcePort);
        }
        else if (strcmp(Name, "destination_port") == 0)
        {
            Valid = ReadPort(Member, &t->DestinationPort);
        }
        else if (strcmp(Name, "rtp_enabled") == 0)
        {
            Valid = ReadBool(Member, &t->RtpEnabled);
        }
        else
        {
            return "transport_params holds a parameter that RTP does not have.";
        }
        if (!Valid)
        {
            return "A transport parameter has a value of the wrong type.";
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies into flow, whose strings and arrays store then holds, the flow of media on the
// first leg of the SDP text. Returns false when text is no SDP or describes no such flow.
//
static bool FindFlow(DtNmosMedia Media, const char* Text, NmosStore* Store,
                     DtNmosFlow* Flow)
{
    DtNmosSdp* Sdp = NULL;
    if (DtNmosSdp_Parse(Text, strlen(Text), &Sdp) != DTNMOS_OK)
    {
        return false;
    }
    bool Found = false;
    for (size_t i = 0; !Found && i < DtNmosSdp_FlowCount(Sdp); ++i)
    {
        const DtNmosFlow* Candidate = DtNmosSdp_Flow(Sdp, i);
        if (Candidate->Media == Media && Candidate->Leg == 0)
        {
            Found = NmosFlow_Copy(Flow, Store, Candidate) == DTNMOS_OK;
        }
    }
    DtNmosSdp_Free(Sdp);
    return Found;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeTransportFile -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets the transport parameters t of a receiver of media to those of the flow that its
// transport file describes, as IS-05 asks of a receiver given one: the multicast group,
// the source and the port. Returns false when the file describes no such flow.
//
static bool TakeTransportFile(DtNmosMedia Media, const char* File, NmosLeg* t)
{
    NmosStore Store;
    memset(&Store, 0, sizeof(Store));
    DtNmosFlow Flow;
    memset(&Flow, 0, sizeof(Flow));
    const bool Found = FindFlow(Media, File, &Store, &Flow);
    if (Found)
    {
        snprintf(t->MulticastIp, sizeof(t->MulticastIp), "%s",
                 NmosNode_IsMulticast(Flow.DestinationIp) ? Flow.DestinationIp : "");
        snprintf(t->SourceIp, sizeof(t->SourceIp), "%s", Flow.SourceIp);
        t->DestinationPort = Flow.DestinationPort;
    }
    NmosStore_Free(&Store);
    return Found;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MergePatch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Merges the body of a PATCH into staged, of a sender or of a receiver of media, and sets
// activate when it asks for an immediate activation. Returns a message on failure, with
// its status in status.
//
static const char* MergePatch(const NmosJson* Body, bool Sender, DtNmosMedia Media,
                              NmosParameters* Staged, int* Activate, int* Status)
{
    *Status = 400;
    if (Body->Type != DTNMOS_JSON_OBJECT)
    {
        return "The body of a PATCH is a JSON object.";
    }
    bool TookFile = false;
    const NmosJson* Params = NULL;
    for (size_t i = 0; i < Body->Count; ++i)
    {
        const char* Key = Body->Keys[i];
        const NmosJson* Value = &Body->Items[i];
        const char* Failure = NULL;
        if (strcmp(Key, "master_enable") == 0)
        {
            if (!ReadBool(Value, &Staged->MasterEnable))
            {
                Failure = "master_enable is true or false.";
            }
        }
        else if (strcmp(Key, Sender ? "receiver_id" : "sender_id") == 0)
        {
            if (!ReadAddress(Value, Staged->PeerId, sizeof(Staged->PeerId)))
            {
                Failure = "The ID of the peer is a UUID or null.";
            }
        }
        else if (!Sender && strcmp(Key, "transport_file") == 0)
        {
            const NmosJson* Data = NmosJson_Member(Value, "data");
            if (Data == NULL ||
                (Data->Type != DTNMOS_JSON_STRING && Data->Type != DTNMOS_JSON_NULL))
            {
                return "transport_file holds data: an SDP, or null.";
            }
            char* File = Data->Type == DTNMOS_JSON_STRING ? CopyText(Data->String) : NULL;
            if (Data->Type == DTNMOS_JSON_STRING && File == NULL)
            {
                *Status = 500;
                return "Out of memory.";
            }
            free(Staged->TransportFile);
            Staged->TransportFile = File;
            TookFile = File != NULL;
        }
        else if (strcmp(Key, "transport_params") == 0)
        {
            Params = Value;
        }
        else if (strcmp(Key, "activation") == 0)
        {
            const NmosJson* Mode = NmosJson_Member(Value, "mode");
            const char* Name = NmosJson_Text(Mode);
            if (Mode == NULL || (Mode->Type != DTNMOS_JSON_NULL && Name == NULL))
            {
                return "activation holds a mode.";
            }
            Failure = ReadActivation(Name, NmosJson_Member(Value, "requested_time"),
                                     Staged, Activate);
        }
        else
        {
            return "The staged parameters have no such member.";
        }
        if (Failure != NULL)
        {
            return Failure;
        }
    }
    // A receiver given a transport file takes the parameters of its flow, which the
    // transport parameters of the same PATCH then override.
    if (TookFile && !TakeTransportFile(Media, Staged->TransportFile, &Staged->Transport))
    {
        return "The transport file describes no flow that the receiver receives.";
    }
    return Params != NULL ? MergeLeg(Params, Sender, &Staged->Transport) : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindConnection -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Finds the sender or receiver id; returns its connection, or null.
//
static NmosConnection* FindConnection(DtNmosNode* Node, const char* Id, bool Sender,
                                      NmosNodeSender** s, NmosNodeReceiver** r)
{
    *s = NULL;
    *r = NULL;
    DtNmosId Wanted;
    memset(&Wanted, 0, sizeof(Wanted));
    if (strlen(Id) >= sizeof(Wanted.Text))
    {
        return NULL;
    }
    strcpy(Wanted.Text, Id);
    if (Sender)
    {
        *s = NmosNode_FindSender(Node, &Wanted);
        return *s != NULL ? (*s)->Connection : NULL;
    }
    *r = NmosNode_FindReceiver(Node, &Wanted);
    return *r != NULL ? (*r)->Connection : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReceiverFlow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fills flow with what a receiver of media receives by staged: the flow of that media in
// its transport file, with the transport parameters over it, which took the values of the
// transport file when it was staged. Returns false when the transport file describes
// none.
//
static bool ReceiverFlow(DtNmosMedia Media, const NmosParameters* Staged,
                         NmosStore* Store, DtNmosFlow* Flow)
{
    if (!FindFlow(Media, Staged->TransportFile, Store, Flow))
    {
        return false;
    }
    const NmosLeg* t = &Staged->Transport;
    if (t->MulticastIp[0] != '\0')
    {
        snprintf(Flow->DestinationIp, sizeof(Flow->DestinationIp), "%s", t->MulticastIp);
    }
    snprintf(Flow->SourceIp, sizeof(Flow->SourceIp), "%s", t->SourceIp);
    if (t->DestinationPort >= 0)
    {
        Flow->DestinationPort = (uint16_t)t->DestinationPort;
    }
    return true;
}

// What an activation hands to the callback of a sender or receiver, gathered under the
// lock of the node and applied without it.
typedef struct NmosActivation
{
    NmosCallback* Callback; // of the sender or receiver; a call takes a use of it
    DtNmosId Resource;
    DtNmosSenderActivation Sender;
    DtNmosReceiverActivation Receiver;
    NmosStore Store; // the strings and arrays of the flow of receiver
} NmosActivation;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearActivation -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void ClearActivation(NmosActivation* a)
{
    NmosStore_Free(&a->Store);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CallsActivation -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns whether the activation a has a callback to call.
//
static bool CallsActivation(const NmosActivation* a)
{
    return a->Callback != NULL &&
           (a->Callback->Sender != NULL || a->Callback->Receiver != NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CallActivation -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Calls the callback that applies the activation a, without the lock, with a use of it
// that the caller took. A callback that fails leaves its message with
// DtNmos_SetLastError(), on this thread.
//
static DtNmosResult CallActivation(const NmosActivation* a)
{
    NmosError_Clear();
    const NmosCallback* Callback = a->Callback;
    if (Callback->Sender != NULL)
    {
        return Callback->Sender(Callback->User, &a->Resource, &a->Sender);
    }
    return Callback->Receiver(Callback->User, &a->Resource, &a->Receiver);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GatherActivation -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Gathers the activation of staged on the sender s or the receiver r. Returns a message
// when staged cannot be activated.
//
static const char* GatherActivation(const NmosNodeSender* s, const NmosNodeReceiver* r,
                                    const NmosParameters* Staged, NmosActivation* a)
{
    const NmosLeg* t = &Staged->Transport;
    const bool Enabled = Staged->MasterEnable && t->RtpEnabled;
    if (s != NULL)
    {
        a->Callback = s->Callback;
        a->Resource = s->Id;
        a->Sender.MasterEnable = Enabled;
        a->Sender.AtNs = Staged->DueNs != 0 ? Staged->DueNs : NmosOs_TaiNowNs();
        const bool Automatic =
            t->DestinationIp[0] == '\0' || strcmp(t->DestinationIp, "auto") == 0;
        snprintf(a->Sender.DestinationIp, sizeof(a->Sender.DestinationIp), "%s",
                 Automatic ? s->Flow.DestinationIp : t->DestinationIp);
        snprintf(a->Sender.SourceIp, sizeof(a->Sender.SourceIp), "%s",
                 IsAuto(t->SourceIp) ? s->SourceIp : t->SourceIp);
        a->Sender.DestinationPort = t->DestinationPort >= 0 ? (uint16_t)t->DestinationPort
                                                            : s->Flow.DestinationPort;
        return NULL;
    }
    a->Callback = r->Callback;
    a->Resource = r->Id;
    a->Receiver.MasterEnable = Enabled;
    a->Receiver.AtNs = Staged->DueNs != 0 ? Staged->DueNs : NmosOs_TaiNowNs();
    snprintf(a->Receiver.SenderId.Text, sizeof(a->Receiver.SenderId.Text), "%s",
             Staged->PeerId);
    if (Staged->TransportFile != NULL)
    {
        a->Receiver.HasFlow =
            ReceiverFlow(r->Media, Staged, &a->Store, &a->Receiver.Flow);
        if (!a->Receiver.HasFlow)
        {
            return "The transport file describes no flow that the receiver receives.";
        }
    }
    else
    {
        // Without a transport file the format is unknown, and the flow carries the
        // transport alone: the group, or for unicast the receiver's own address, the
        // source and the port, "auto" resolved.
        DtNmosFlow* Flow = &a->Receiver.Flow;
        Flow->Size = sizeof(*Flow);
        Flow->Media = r->Media;
        const char* Destination = t->MulticastIp[0] != '\0' ? t->MulticastIp
                                  : IsAuto(t->InterfaceIp)  ? r->InterfaceIp
                                                            : t->InterfaceIp;
        snprintf(Flow->DestinationIp, sizeof(Flow->DestinationIp), "%s", Destination);
        snprintf(Flow->SourceIp, sizeof(Flow->SourceIp), "%s", t->SourceIp);
        Flow->DestinationPort =
            (uint16_t)(t->DestinationPort >= 0 ? t->DestinationPort : NMOS_RTP_PORT);
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeActive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes staged, activated, the active parameters of the sender s or receiver r, with each
// "auto" resolved, and registers the new state of the sender or receiver.
//
static void MakeActive(DtNmosNode* Node, NmosConnection* c, NmosNodeSender* s,
                       NmosNodeReceiver* r, NmosParameters* Staged)
{
    NmosOs_VersionNow(&Node->LastVersion, Staged->ActivationTime,
                      sizeof(Staged->ActivationTime));
    if (!CopyParameters(&c->Active, Staged))
    {
        return;
    }
    if (s != NULL)
    {
        NmosLeg* t = &c->Active.Transport;
        if (t->DestinationIp[0] != '\0' && strcmp(t->DestinationIp, "auto") != 0)
        {
            snprintf(s->Flow.DestinationIp, sizeof(s->Flow.DestinationIp), "%s",
                     t->DestinationIp);
        }
        if (t->DestinationPort >= 0)
        {
            s->Flow.DestinationPort = (uint16_t)t->DestinationPort;
        }
        snprintf(t->DestinationIp, sizeof(t->DestinationIp), "%s", s->Flow.DestinationIp);
        t->DestinationPort = s->Flow.DestinationPort;
        if (t->SourcePort < 0)
        {
            t->SourcePort = s->Flow.DestinationPort;
        }
        if (IsAuto(t->SourceIp))
        {
            snprintf(t->SourceIp, sizeof(t->SourceIp), "%s", s->SourceIp);
        }
        if (strcmp(s->ActiveSourceIp, t->SourceIp) != 0)
        {
            // The node lists the interface of the address its sender sends from.
            snprintf(s->ActiveSourceIp, sizeof(s->ActiveSourceIp), "%s", t->SourceIp);
            NmosNode_Touch(Node);
        }
        s->MasterEnable = c->Active.MasterEnable && t->RtpEnabled;
        // A sender that is parked is subscribed to no receiver in IS-04.
        snprintf(s->ReceiverId.Text, sizeof(s->ReceiverId.Text), "%s",
                 s->MasterEnable ? Staged->PeerId : "");
        ++s->SessionVersion;
        NmosOs_VersionNow(&Node->LastVersion, s->Version, sizeof(s->Version));
        s->Registered = false;
    }
    else
    {
        // The port, left to the node, is that of the flow of the transport file, or the
        // port of RTP.
        NmosLeg* t = &c->Active.Transport;
        if (IsAuto(t->InterfaceIp))
        {
            snprintf(t->InterfaceIp, sizeof(t->InterfaceIp), "%s", r->InterfaceIp);
        }
        if (t->DestinationPort < 0)
        {
            NmosStore Store;
            memset(&Store, 0, sizeof(Store));
            DtNmosFlow Flow;
            memset(&Flow, 0, sizeof(Flow));
            t->DestinationPort =
                c->Active.TransportFile != NULL &&
                        FindFlow(r->Media, c->Active.TransportFile, &Store, &Flow)
                    ? Flow.DestinationPort
                    : NMOS_RTP_PORT;
            NmosStore_Free(&Store);
        }
        r->MasterEnable = c->Active.MasterEnable && t->RtpEnabled;
        // A receiver that is parked is subscribed to no sender in IS-04.
        snprintf(r->SenderId.Text, sizeof(r->SenderId.Text), "%s",
                 r->MasterEnable ? Staged->PeerId : "");
        NmosOs_VersionNow(&Node->LastVersion, r->Version, sizeof(r->Version));
        r->Registered = false;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PatchStaged -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers a PATCH, Body, of the staged parameters of the sender or receiver Id: with 200
// and the activation that took place, or with 202 and the one that is scheduled. While
// one is scheduled, only a PATCH that cancels it is taken; while a callback applies one,
// none is, as what it applies cannot be taken back (423).
//
static void PatchJson(DtNmosNode* Node, const char* Id, bool Sender, const NmosJson* Body,
                      DtNmosHttpResponse* Response)
{
    const NmosJson* Asked =
        Body->Type == DTNMOS_JSON_OBJECT ? NmosJson_Member(Body, "activation") : NULL;
    const NmosJson* AskedMode = Asked != NULL && Asked->Type == DTNMOS_JSON_OBJECT
                                    ? NmosJson_Member(Asked, "mode")
                                    : NULL;
    const bool Cancels = AskedMode != NULL && AskedMode->Type == DTNMOS_JSON_NULL;
    NmosParameters Staged;
    memset(&Staged, 0, sizeof(Staged));
    NmosActivation a;
    memset(&a, 0, sizeof(a));
    int Activate = NMOS_ACTIVATE_NONE;
    int Status = 404;
    const char* Failure = "The node has no such sender or receiver.";

    NmosNode_Lock(Node);
    NmosNodeSender* s = NULL;
    NmosNodeReceiver* r = NULL;
    NmosConnection* c = FindConnection(Node, Id, Sender, &s, &r);
    if (c != NULL && c->Applying)
    {
        // The callback is applying an activation, which cannot be taken back; the PATCH
        // waits for it to end, a cancel included.
        Status = 423;
        Failure = "An activation is being applied; try again when it has taken place.";
    }
    else if (c != NULL && c->Staged.DueNs != 0 && !Cancels)
    {
        Status = 423;
        Failure = "An activation is scheduled; a PATCH with an activation of mode null "
                  "cancels it.";
    }
    else if (c != NULL)
    {
        Status = 500;
        Failure = "Out of memory.";
        if (CopyParameters(&Staged, &c->Staged))
        {
            Failure = MergePatch(Body, Sender, r != NULL ? r->Media : s->Flow.Media,
                                 &Staged, &Activate, &Status);
        }
    }
    if (Failure == NULL && Activate != NMOS_ACTIVATE_NONE)
    {
        Status = 400;
        Failure = GatherActivation(s, r, &Staged, &a);
    }
    if (Failure == NULL && Activate == NMOS_ACTIVATE_LATER)
    {
        // Checked now, applied when it is due, by the poll that the node wakes for it.
        ClearActivation(&a);
        memset(&a, 0, sizeof(a));
        Node->Wake = true;
    }
    if (Failure == NULL && !CopyParameters(&c->Staged, &Staged))
    {
        Status = 500;
        Failure = "Out of memory.";
    }
    const bool Calls = Failure == NULL && CallsActivation(&a);
    if (Calls)
    {
        c->Applying = true;
        ++a.Callback->Uses;
    }
    NmosNode_Unlock(Node);

    // The callback applies the activation without the lock, for as long as that takes.
    const DtNmosResult Result = Calls ? CallActivation(&a) : DTNMOS_OK;
    ClearActivation(&a);
    if (Result != DTNMOS_OK)
    {
        Status = 500;
        Failure = DtNmos_GetLastError()[0] != '\0' ? DtNmos_GetLastError()
                                                   : "The activation failed.";
    }

    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    NmosCallback* Released = NULL;
    if (Calls || Failure == NULL)
    {
        NmosNode_Lock(Node);
        if (Calls)
        {
            NmosCallback_Drop(a.Callback, &Released);
        }
        c = FindConnection(Node, Id, Sender, &s, &r);
        if (c != NULL)
        {
            c->Applying = false;
        }
        if (Failure == NULL && c == NULL)
        {
            Status = 404;
            Failure = "The sender or receiver was removed during its activation.";
        }
        else if (Failure == NULL)
        {
            if (Activate == NMOS_ACTIVATE_NOW)
            {
                MakeActive(Node, c, s, r, &Staged);
            }
            // The answer shows the activation that took place, or is scheduled.
            WriteParameters(&b, &Staged, Sender, Activate != NMOS_ACTIVATE_NONE);
        }
        NmosNode_Unlock(Node);
    }
    free(Staged.TransportFile);
    if (Failure != NULL)
    {
        NmosNode_AnswerError(Response, Status, Failure);
    }
    else
    {
        NmosNode_AnswerJson(Response, &b);
        if (Activate == NMOS_ACTIVATE_LATER && !b.Failed)
        {
            DtNmosHttpResponse_SetStatus(Response, 202);
        }
    }
    NmosBuffer_Free(&b);
    // A sender or receiver removed during its activation is released now, its last call
    // done, and after the answer, whose reason may be the message of this thread.
    NmosCallback_ReleaseAll(Released);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PatchStaged -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers a PATCH of the staged parameters of the sender or receiver Id.
//
static void PatchStaged(DtNmosNode* Node, const char* Id, bool Sender,
                        const DtNmosHttpRequest* Request, DtNmosHttpResponse* Response)
{
    NmosJson* Body = NULL;
    if (Request->Body == NULL ||
        NmosJson_Parse(Request->Body, Request->BodyLength, &Body) != DTNMOS_OK)
    {
        NmosNode_AnswerError(Response, 400, "The body of the PATCH is no JSON.");
        return;
    }
    PatchJson(Node, Id, Sender, Body, Response);
    NmosJson_Free(Body);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PostBulk -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers a POST to the bulk interface of the senders or the receivers: an array of
// patches, each with the ID of what it patches and its params, which are applied in turn
// as a PATCH of each would be, and answered with the status and the error of each.
//
static void PostBulk(DtNmosNode* Node, bool Sender, const DtNmosHttpRequest* Request,
                     DtNmosHttpResponse* Response)
{
    NmosJson* Body = NULL;
    if (Request->Body == NULL ||
        NmosJson_Parse(Request->Body, Request->BodyLength, &Body) != DTNMOS_OK ||
        Body->Type != DTNMOS_JSON_ARRAY)
    {
        NmosJson_Free(Body);
        NmosNode_AnswerError(Response, 400,
                             "The body of a bulk request is an array of patches.");
        return;
    }
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    DTNMOS_APPEND_LITERAL(&b, "[");
    for (size_t i = 0; i < Body->Count; ++i)
    {
        const NmosJson* Item = &Body->Items[i];
        const bool Object = Item->Type == DTNMOS_JSON_OBJECT;
        const char* Id = Object ? NmosJson_MemberText(Item, "id") : NULL;
        const NmosJson* Params = Object ? NmosJson_Member(Item, "params") : NULL;
        DtNmosHttpResponse* One = DtNmosHttpResponse_Alloc();
        if (One == NULL)
        {
            b.Failed = true;
            break;
        }
        if (Id == NULL || Params == NULL)
        {
            NmosNode_AnswerError(One, 400,
                                 "Each patch of a bulk request has an id and params.");
        }
        else
        {
            PatchJson(Node, Id, Sender, Params, One);
        }
        const int Code = DtNmosHttpResponse_Status(One);
        if (i > 0)
        {
            DTNMOS_APPEND_LITERAL(&b, ", ");
        }
        DTNMOS_APPEND_LITERAL(&b, "{\"id\": ");
        WriteTextOrNull(&b, Id != NULL ? Id : "");
        NmosBuffer_Printf(&b, ", \"code\": %d", Code);
        if (Code >= 400)
        {
            // The error is the one the PATCH of this patch alone answers with.
            size_t Length = 0;
            const char* Text = DtNmosHttpResponse_Body(One, &Length);
            NmosJson* Answer = NULL;
            const char* Error = NULL;
            if (Text != NULL && NmosJson_Parse(Text, Length, &Answer) == DTNMOS_OK)
            {
                Error = NmosJson_MemberText(Answer, "error");
            }
            DTNMOS_APPEND_LITERAL(&b, ", \"error\": ");
            WriteTextOrNull(&b, Error != NULL ? Error : "");
            DTNMOS_APPEND_LITERAL(&b, ", \"debug\": null");
            NmosJson_Free(Answer);
        }
        DTNMOS_APPEND_LITERAL(&b, "}");
        DtNmosHttpResponse_Free(One);
    }
    DTNMOS_APPEND_LITERAL(&b, "]");
    NmosJson_Free(Body);
    NmosNode_AnswerJson(Response, &b);
    NmosBuffer_Free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindDue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Finds a sender or receiver whose scheduled activation is due at NowNs, its lead before
// the time it asked for; sets *NextNs to when the first that is not due yet is, when one
// is sooner. The caller holds the lock.
//
static NmosConnection* FindDue(DtNmosNode* Node, uint64_t NowNs, NmosNodeSender** s,
                               NmosNodeReceiver** r, uint64_t* NextNs)
{
    *s = NULL;
    *r = NULL;
    NmosConnection* Due = NULL;
    for (size_t i = 0; i < Node->SenderCount + Node->ReceiverCount; ++i)
    {
        const bool IsSender = i < Node->SenderCount;
        NmosConnection* c = IsSender ? Node->Senders[i].Connection
                                     : Node->Receivers[i - Node->SenderCount].Connection;
        const uint64_t Lead = IsSender ? Node->Senders[i].LeadNs
                                       : Node->Receivers[i - Node->SenderCount].LeadNs;
        const uint64_t Time = c->Staged.DueNs;
        const uint64_t At = Time == 0 ? 0 : Time > Lead ? Time - Lead : 1;
        if (At != 0 && At <= NowNs && Due == NULL)
        {
            Due = c;
            *s = IsSender ? &Node->Senders[i] : NULL;
            *r = IsSender ? NULL : &Node->Receivers[i - Node->SenderCount];
        }
        else if (At > NowNs && (*NextNs == 0 || At < *NextNs))
        {
            *NextNs = At;
        }
    }
    return Due;
}

// The margin before a scheduled activation within which the poll waits for it itself.
#define NMOS_DUE_SPIN_NS 3000000u

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitUntil -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Waits until AtNs on the clock of TAI: sleeping to the margin before it, and spinning
// the rest, so that what follows takes place at its time rather than at the next
// millisecond of a sleep. Returns at once for a time that is past.
//
static void WaitUntil(uint64_t AtNs)
{
    for (uint64_t Now = NmosOs_TaiNowNs(); Now < AtNs; Now = NmosOs_TaiNowNs())
    {
        const uint64_t Left = AtNs - Now;
        NmosOs_SleepMs(Left > NMOS_DUE_SPIN_NS
                           ? (uint32_t)((Left - NMOS_DUE_SPIN_NS) / 1000000u)
                           : 0);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosConnection_Poll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void NmosConnection_Poll(DtNmosNode* Node, uint32_t* WaitMs)
{
    for (;;)
    {
        NmosNode_Lock(Node);
        Node->Wake = false;
        const uint64_t Now = NmosOs_TaiNowNs();
        uint64_t Next = 0;
        NmosNodeSender* s = NULL;
        NmosNodeReceiver* r = NULL;
        NmosConnection* c = FindDue(Node, Now, &s, &r, &Next);
        if (c == NULL && Next != 0 && Next - Now <= NMOS_DUE_SPIN_NS)
        {
            // Due within the margin: waited for here, on the clock of TAI, so that it
            // takes place at its time rather than at the next millisecond of a sleep.
            NmosNode_Unlock(Node);
            WaitUntil(Next);
            continue;
        }
        if (c == NULL)
        {
            NmosNode_Unlock(Node);
            // The poll comes back the margin before the next is due.
            const uint64_t Ms =
                Next != 0 ? (Next - Now - NMOS_DUE_SPIN_NS) / 1000000u : 0;
            if (Next != 0 && Ms < *WaitMs)
            {
                *WaitMs = (uint32_t)Ms;
            }
            return;
        }
        // The activation is taken off the schedule before it is applied, once.
        const bool Sender = s != NULL;
        const DtNmosId Id = Sender ? s->Id : r->Id;
        NmosParameters Staged;
        memset(&Staged, 0, sizeof(Staged));
        NmosActivation a;
        memset(&a, 0, sizeof(a));
        const char* Failure = CopyParameters(&Staged, &c->Staged)
                                  ? GatherActivation(s, r, &Staged, &a)
                                  : "Out of memory.";
        c->Staged.DueNs = 0;
        c->Applying = Failure == NULL;
        const bool Calls = Failure == NULL && CallsActivation(&a);
        if (Calls)
        {
            ++a.Callback->Uses;
        }
        NmosNode_Unlock(Node);

        const DtNmosResult Result = Calls ? CallActivation(&a) : DTNMOS_OK;
        ClearActivation(&a);
        if (Failure == NULL && Result != DTNMOS_OK)
        {
            Failure = DtNmos_GetLastError()[0] != '\0' ? DtNmos_GetLastError()
                                                       : "The activation failed.";
        }
        // The reason is kept, as a release may fail on this thread too.
        char Reason[512];
        if (Failure != NULL)
        {
            snprintf(Reason, sizeof(Reason), "%s", Failure);
            Failure = Reason;
        }
        if (Calls)
        {
            // The use of the call ends when the callback returns, not at the time the
            // activation may still wait for below: a sender removed meanwhile is released
            // now.
            NmosCallback* Released = NULL;
            NmosNode_Lock(Node);
            NmosCallback_Drop(a.Callback, &Released);
            NmosNode_Unlock(Node);
            NmosCallback_ReleaseAll(Released);
        }

        // A callback called its lead early that returns before the time asked for: the
        // parameters become active at that time, never before it.
        if (Failure == NULL)
        {
            WaitUntil(Staged.DueNs);
        }

        NmosNode_Lock(Node);
        c = FindConnection(Node, Id.Text, Sender, &s, &r);
        if (c != NULL && Failure == NULL)
        {
            MakeActive(Node, c, s, r, &Staged);
        }
        if (c != NULL)
        {
            // The staged parameters show no activation once it took place, or failed.
            c->Applying = false;
            c->Staged.Mode[0] = '\0';
            c->Staged.RequestedTime[0] = '\0';
            c->Staged.ActivationTime[0] = '\0';
        }
        NmosNode_Unlock(Node);
        free(Staged.TransportFile);
        if (Failure != NULL && Node->Log != NULL)
        {
            char Message[640];
            snprintf(Message, sizeof(Message),
                     "The scheduled activation of %s failed: %s", Id.Text, Failure);
            Node->Log(Node->LogUser, DTNMOS_LOG_WARNING, Message);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AnswerTransportFile -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers the transport file of the sender s.
//
static void AnswerTransportFile(const NmosNodeSender* s, DtNmosHttpResponse* Response)
{
    NmosBuffer Text;
    memset(&Text, 0, sizeof(Text));
    if (NmosNode_WriteTransportFile(s, &Text) != DTNMOS_OK)
    {
        NmosBuffer_Free(&Text);
        NmosNode_AnswerError(Response, 500, DtNmos_GetLastError());
        return;
    }
    DtNmosHttpResponse_SetStatus(Response, 200);
    DtNmosHttpResponse_SetBody(Response, "application/sdp", Text.Data, Text.Length);
    NmosBuffer_Free(&Text);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AnswerIds -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers the IDs of the senders or the receivers, each followed by a slash.
//
static void AnswerIds(const DtNmosNode* Node, bool Sender, DtNmosHttpResponse* Response)
{
    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    DTNMOS_APPEND_LITERAL(&b, "[");
    const size_t Count = Sender ? Node->SenderCount : Node->ReceiverCount;
    for (size_t i = 0; i < Count; ++i)
    {
        NmosBuffer_Printf(&b, "%s\"%s/\"", i == 0 ? "" : ", ",
                          Sender ? Node->Senders[i].Id.Text : Node->Receivers[i].Id.Text);
    }
    DTNMOS_APPEND_LITERAL(&b, "]");
    NmosNode_AnswerJson(Response, &b);
    NmosBuffer_Free(&b);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AnswerSingle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers a GET of the single interface, whose further segments are segments; the caller
// holds the lock.
//
static void AnswerSingle(DtNmosNode* Node, char** Segments, size_t Count,
                         DtNmosHttpResponse* Response)
{
    const bool Sender = Count >= 1 && strcmp(Segments[0], "senders") == 0;
    const bool Receiver = Count >= 1 && strcmp(Segments[0], "receivers") == 0;
    if (Count == 0)
    {
        AnswerText(Response, "[\"senders/\", \"receivers/\"]");
        return;
    }
    if ((!Sender && !Receiver) || Count > 3)
    {
        NmosNode_AnswerError(Response, 404, "The Connection API has no such resource.");
        return;
    }
    if (Count == 1)
    {
        AnswerIds(Node, Sender, Response);
        return;
    }
    NmosNodeSender* s = NULL;
    NmosNodeReceiver* r = NULL;
    NmosConnection* c = FindConnection(Node, Segments[1], Sender, &s, &r);
    if (c == NULL)
    {
        NmosNode_AnswerError(Response, 404, "The node has no such sender or receiver.");
        return;
    }
    const char* Leaf = Count == 3 ? Segments[2] : "";
    if (Count == 2)
    {
        AnswerText(Response, Sender ? "[\"constraints/\", \"staged/\", \"active/\", "
                                      "\"transportfile/\", \"transporttype/\"]"
                                    : "[\"constraints/\", \"staged/\", \"active/\", "
                                      "\"transporttype/\"]");
    }
    else if (strcmp(Leaf, "constraints") == 0)
    {
        AnswerText(Response, Sender ? SenderConstraints : ReceiverConstraints);
    }
    else if (strcmp(Leaf, "transporttype") == 0)
    {
        AnswerText(Response, "\"urn:x-nmos:transport:rtp\"");
    }
    else if (strcmp(Leaf, "staged") == 0 || strcmp(Leaf, "active") == 0)
    {
        const bool Active = strcmp(Leaf, "active") == 0;
        NmosBuffer b;
        memset(&b, 0, sizeof(b));
        // The staged parameters show their activation while it is scheduled.
        WriteParameters(&b, Active ? &c->Active : &c->Staged, Sender,
                        Active || c->Staged.DueNs != 0);
        NmosNode_AnswerJson(Response, &b);
        NmosBuffer_Free(&b);
    }
    else if (Sender && strcmp(Leaf, "transportfile") == 0)
    {
        AnswerTransportFile(s, Response);
    }
    else
    {
        NmosNode_AnswerError(Response, 404, "The Connection API has no such resource.");
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosConnection_Handle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtNmosResult NmosConnection_Handle(DtNmosNode* Node, const DtNmosHttpRequest* Request,
                                   char** Segments, size_t Count,
                                   DtNmosHttpResponse* Response)
{
    const bool Single = Count >= 1 && strcmp(Segments[0], "single") == 0;
    const bool Bulk = Count >= 1 && strcmp(Segments[0], "bulk") == 0;
    if (strcmp(Request->Method, "PATCH") == 0)
    {
        if (Single && Count == 4 && strcmp(Segments[3], "staged") == 0 &&
            (strcmp(Segments[1], "senders") == 0 ||
             strcmp(Segments[1], "receivers") == 0))
        {
            PatchStaged(Node, Segments[2], strcmp(Segments[1], "senders") == 0, Request,
                        Response);
        }
        else
        {
            NmosNode_AnswerError(Response, 405, "Only staged parameters take a PATCH.");
        }
        return DTNMOS_OK;
    }
    if (Bulk && Count == 2 && strcmp(Request->Method, "POST") == 0)
    {
        if (strcmp(Segments[1], "senders") == 0 || strcmp(Segments[1], "receivers") == 0)
        {
            PostBulk(Node, strcmp(Segments[1], "senders") == 0, Request, Response);
        }
        else
        {
            NmosNode_AnswerError(Response, 404,
                                 "The Connection API has no such resource.");
        }
        return DTNMOS_OK;
    }
    if (strcmp(Request->Method, "GET") != 0 && strcmp(Request->Method, "HEAD") != 0)
    {
        NmosNode_AnswerError(Response, 405, "The Connection API answers GET and PATCH.");
        return DTNMOS_OK;
    }
    NmosNode_Lock(Node);
    if (Count == 0)
    {
        AnswerText(Response, "[\"bulk/\", \"single/\"]");
    }
    else if (Single)
    {
        AnswerSingle(Node, Segments + 1, Count - 1, Response);
    }
    else if (Bulk && Count == 1)
    {
        AnswerText(Response, "[\"senders/\", \"receivers/\"]");
    }
    else if (Bulk && Count == 2)
    {
        NmosNode_AnswerError(Response, 405, "The bulk interface takes a POST.");
    }
    else
    {
        NmosNode_AnswerError(Response, 404, "The Connection API has no such resource.");
    }
    NmosNode_Unlock(Node);
    return DTNMOS_OK;
}
