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
    int RtpEnabled;
} NmosLeg;

// The staged or active parameters of a sender or receiver.
typedef struct NmosParameters
{
    int MasterEnable;
    char PeerId[37]; // receiver_id of a sender, sender_id of a receiver; "" for null
    NmosLeg Transport;
    char* TransportFile;     // of a receiver: the SDP it was given, or null
    char ActivationTime[32]; // of the last activation; "" for null
} NmosParameters;

typedef struct NmosConnection
{
    NmosParameters Staged;
    NmosParameters Active;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-. IsAuto .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether an address the node chooses is left to it: "auto", or null.
//
static int IsAuto(const char* Address)
{
    return Address[0] == '\0' || strcmp(Address, "auto") == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyParameters -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes target a copy of source; returns 0 when out of memory, leaving target as it was.
//
static int CopyParameters(NmosParameters* Target, const NmosParameters* Source)
{
    char* File = CopyText(Source->TransportFile);
    if (Source->TransportFile != NULL && File == NULL)
    {
        return 0;
    }
    free(Target->TransportFile);
    *Target = *Source;
    Target->TransportFile = File;
    return 1;
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
    t->RtpEnabled = 1;
    c->Active.MasterEnable = 1;
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
DtNmosResult NmosConnection_InitReceiver(NmosNodeReceiver* Receiver)
{
    NmosConnection* c = calloc(1, sizeof(*c));
    if (c == NULL)
    {
        return DTNMOS_E_NO_MEMORY;
    }
    // A receiver receives what its element was given until a controller connects it: from
    // any source, on its port, at the port of RTP.
    NmosLeg* t = &c->Active.Transport;
    snprintf(t->InterfaceIp, sizeof(t->InterfaceIp), "%s", Receiver->InterfaceIp);
    t->DestinationPort = NMOS_RTP_PORT;
    t->RtpEnabled = 1;
    c->Active.MasterEnable = 1;
    c->Staged = c->Active;
    Receiver->Connection = c;
    Receiver->MasterEnable = 1;
    return DTNMOS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- NmosConnection_ClearReceiver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void NmosConnection_ClearReceiver(NmosNodeReceiver* Receiver)
{
    FreeConnection(Receiver->Connection);
    Receiver->Connection = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes an address, or null for "".
//
static void WriteAddress(NmosBuffer* b, const char* Address)
{
    if (Address[0] == '\0')
    {
        DTNMOS_APPEND_LITERAL(b, "null");
    }
    else
    {
        NmosJson_WriteString(b, Address);
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
// Writes the parameters p of a sender or receiver; with_activation writes the immediate
// activation of p, which staged parameters show only in the answer to their PATCH.
//
static void WriteParameters(NmosBuffer* b, const NmosParameters* p, int Sender,
                            int WithActivation)
{
    NmosBuffer_Printf(b, "{\"%s\": ", Sender ? "receiver_id" : "sender_id");
    WriteAddress(b, p->PeerId);
    NmosBuffer_Printf(b, ", \"master_enable\": %s, \"activation\": ",
                      p->MasterEnable ? "true" : "false");
    if (WithActivation && p->ActivationTime[0] != '\0')
    {
        NmosBuffer_Printf(b,
                          "{\"mode\": \"activate_immediate\", \"requested_time\": null, "
                          "\"activation_time\": \"%s\"}",
                          p->ActivationTime);
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
    WriteAddress(b, p->Transport.SourceIp);
    if (Sender)
    {
        DTNMOS_APPEND_LITERAL(b, ", \"destination_ip\": ");
        WriteAddress(b, p->Transport.DestinationIp);
        DTNMOS_APPEND_LITERAL(b, ", \"source_port\": ");
        WritePort(b, p->Transport.SourcePort);
    }
    else
    {
        DTNMOS_APPEND_LITERAL(b, ", \"multicast_ip\": ");
        WriteAddress(b, p->Transport.MulticastIp);
        DTNMOS_APPEND_LITERAL(b, ", \"interface_ip\": ");
        WriteAddress(b, p->Transport.InterfaceIp);
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
// Reads an address: a string, or null into "". Returns 0 for another type.
//
static int ReadAddress(const NmosJson* Value, char* Target, size_t Size)
{
    if (Value->Type == DTNMOS_JSON_NULL)
    {
        Target[0] = '\0';
        return 1;
    }
    if (Value->Type != DTNMOS_JSON_STRING || Value->StringLength >= Size)
    {
        return 0;
    }
    memcpy(Target, Value->String, Value->StringLength + 1);
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads a port: a whole number of 0 to 65535, or "auto" into -1.
//
static int ReadPort(const NmosJson* Value, int* Port)
{
    if (Value->Type == DTNMOS_JSON_STRING && strcmp(Value->String, "auto") == 0)
    {
        *Port = -1;
        return 1;
    }
    if (Value->Type != DTNMOS_JSON_NUMBER || Value->Number < 0 || Value->Number > 65535 ||
        (double)(int)Value->Number != Value->Number)
    {
        return 0;
    }
    *Port = (int)Value->Number;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadBool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int ReadBool(const NmosJson* Value, int* Target)
{
    if (Value->Type != DTNMOS_JSON_TRUE && Value->Type != DTNMOS_JSON_FALSE)
    {
        return 0;
    }
    *Target = Value->Type == DTNMOS_JSON_TRUE;
    return 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MergeLeg -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Merges the transport parameters of the one leg into t; returns a message on failure.
//
static const char* MergeLeg(const NmosJson* Value, int Sender, NmosLeg* t)
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
        int Valid = 0;
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
// first leg of the SDP text. Returns 0 when text is no SDP or describes no such flow.
//
static int FindFlow(DtNmosMedia Media, const char* Text, NmosStore* Store,
                    DtNmosFlow* Flow)
{
    DtNmosSdp* Sdp = NULL;
    if (DtNmosSdp_Parse(Text, strlen(Text), &Sdp) != DTNMOS_OK)
    {
        return 0;
    }
    int Found = 0;
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
// the source and the port. Returns 0 when the file describes no such flow.
//
static int TakeTransportFile(DtNmosMedia Media, const char* File, NmosLeg* t)
{
    NmosStore Store;
    memset(&Store, 0, sizeof(Store));
    DtNmosFlow Flow;
    memset(&Flow, 0, sizeof(Flow));
    const int Found = FindFlow(Media, File, &Store, &Flow);
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
static const char* MergePatch(const NmosJson* Body, int Sender, DtNmosMedia Media,
                              NmosParameters* Staged, int* Activate, int* Status)
{
    *Status = 400;
    if (Body->Type != DTNMOS_JSON_OBJECT)
    {
        return "The body of a PATCH is a JSON object.";
    }
    int TookFile = 0;
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
            if (Name != NULL && strcmp(Name, "activate_immediate") == 0)
            {
                *Activate = 1;
            }
            else if (Name != NULL)
            {
                *Status = 501;
                return "The node activates immediately only.";
            }
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
static NmosConnection* FindConnection(DtNmosNode* Node, const char* Id, int Sender,
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
// transport file when it was staged. Returns 0 when the transport file describes none.
//
static int ReceiverFlow(DtNmosMedia Media, const NmosParameters* Staged, NmosStore* Store,
                        DtNmosFlow* Flow)
{
    if (!FindFlow(Media, Staged->TransportFile, Store, Flow))
    {
        return 0;
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
    return 1;
}

// What an activation hands to the callback of a sender or receiver, gathered under the
// lock of the node and applied without it.
typedef struct NmosActivation
{
    DtNmosSenderActivateFunc SenderCallback;
    DtNmosReceiverActivateFunc ReceiverCallback;
    void* User;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GatherActivation -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Gathers the activation of staged on the sender s or the receiver r. Returns a message
// when staged cannot be activated.
//
static const char* GatherActivation(const NmosNodeSender* s, const NmosNodeReceiver* r,
                                    const NmosParameters* Staged, NmosActivation* a)
{
    const NmosLeg* t = &Staged->Transport;
    const int Enabled = Staged->MasterEnable && t->RtpEnabled;
    if (s != NULL)
    {
        a->SenderCallback = s->Activate;
        a->User = s->User;
        a->Resource = s->Id;
        a->Sender.MasterEnable = Enabled;
        const int Automatic =
            t->DestinationIp[0] == '\0' || strcmp(t->DestinationIp, "auto") == 0;
        snprintf(a->Sender.DestinationIp, sizeof(a->Sender.DestinationIp), "%s",
                 Automatic ? s->Flow.DestinationIp : t->DestinationIp);
        snprintf(a->Sender.SourceIp, sizeof(a->Sender.SourceIp), "%s",
                 IsAuto(t->SourceIp) ? s->SourceIp : t->SourceIp);
        a->Sender.DestinationPort = t->DestinationPort >= 0 ? (uint16_t)t->DestinationPort
                                                            : s->Flow.DestinationPort;
        return NULL;
    }
    a->ReceiverCallback = r->Activate;
    a->User = r->User;
    a->Resource = r->Id;
    a->Receiver.MasterEnable = Enabled;
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
        s->Registered = 0;
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
        r->Registered = 0;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PatchStaged -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Answers a PATCH of the staged parameters of the sender or receiver id.
//
static void PatchStaged(DtNmosNode* Node, const char* Id, int Sender,
                        const DtNmosHttpRequest* Request, DtNmosHttpResponse* Response)
{
    NmosJson* Body = NULL;
    if (Request->Body == NULL ||
        NmosJson_Parse(Request->Body, Request->BodyLength, &Body) != DTNMOS_OK)
    {
        NmosNode_AnswerError(Response, 400, "The body of the PATCH is no JSON.");
        return;
    }
    NmosParameters Staged;
    memset(&Staged, 0, sizeof(Staged));
    NmosActivation a;
    memset(&a, 0, sizeof(a));
    int Activate = 0;
    int Status = 404;
    const char* Failure = "The node has no such sender or receiver.";

    NmosNode_Lock(Node);
    NmosNodeSender* s = NULL;
    NmosNodeReceiver* r = NULL;
    NmosConnection* c = FindConnection(Node, Id, Sender, &s, &r);
    if (c != NULL)
    {
        Status = 500;
        Failure = "Out of memory.";
        if (CopyParameters(&Staged, &c->Staged))
        {
            Failure = MergePatch(Body, Sender, r != NULL ? r->Media : s->Flow.Media,
                                 &Staged, &Activate, &Status);
        }
    }
    if (Failure == NULL && Activate)
    {
        Status = 400;
        Failure = GatherActivation(s, r, &Staged, &a);
    }
    if (Failure == NULL && !CopyParameters(&c->Staged, &Staged))
    {
        Status = 500;
        Failure = "Out of memory.";
    }
    NmosNode_Unlock(Node);
    NmosJson_Free(Body);

    // The callback applies the activation without the lock, for as long as that takes. A
    // callback that fails leaves its message with DtNmos_SetLastError(), on this thread.
    NmosError_Clear();
    DtNmosResult Result = DTNMOS_OK;
    if (Failure == NULL && a.SenderCallback != NULL)
    {
        Result = a.SenderCallback(a.User, &a.Resource, &a.Sender);
    }
    else if (Failure == NULL && a.ReceiverCallback != NULL)
    {
        Result = a.ReceiverCallback(a.User, &a.Resource, &a.Receiver);
    }
    ClearActivation(&a);
    if (Result != DTNMOS_OK)
    {
        Status = 500;
        Failure = DtNmos_GetLastError()[0] != '\0' ? DtNmos_GetLastError()
                                                   : "The activation failed.";
    }

    NmosBuffer b;
    memset(&b, 0, sizeof(b));
    if (Failure == NULL)
    {
        NmosNode_Lock(Node);
        c = FindConnection(Node, Id, Sender, &s, &r);
        if (c == NULL)
        {
            Status = 404;
            Failure = "The sender or receiver was removed during its activation.";
        }
        else
        {
            if (Activate)
            {
                MakeActive(Node, c, s, r, &Staged);
            }
            // The answer shows the activation that took place.
            WriteParameters(&b, &Staged, Sender, Activate);
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
    }
    NmosBuffer_Free(&b);
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
static void AnswerIds(const DtNmosNode* Node, int Sender, DtNmosHttpResponse* Response)
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
    const int Sender = Count >= 1 && strcmp(Segments[0], "senders") == 0;
    const int Receiver = Count >= 1 && strcmp(Segments[0], "receivers") == 0;
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
        const int Active = strcmp(Leaf, "active") == 0;
        NmosBuffer b;
        memset(&b, 0, sizeof(b));
        WriteParameters(&b, Active ? &c->Active : &c->Staged, Sender, Active);
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
    const int Single = Count >= 1 && strcmp(Segments[0], "single") == 0;
    const int Bulk = Count >= 1 && strcmp(Segments[0], "bulk") == 0;
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
        NmosNode_AnswerError(Response, 501,
                             "The node does not implement bulk activation.");
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
