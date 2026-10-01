// #*#*#*#*#*#*#*#*#*#*#*#*#*# ExampleCommon.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// dtnmos - What the example programs share - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// With -std=c11 the C library declares only ISO C. Asked for before any header, this
// also exposes nanosleep.
#ifndef _WIN32
    #define _POSIX_C_SOURCE 199309L
#endif

#include "ExampleCommon.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <time.h>
#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindOption -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The option named Name, or null when the program has none.
//
static const ExampleOption* FindOption(const char* Name, const ExampleOption* Options,
                                       int NumOptions)
{
    for (int i = 0; i < NumOptions; i++)
    {
        if (strcmp(Options[i].Name, Name) == 0)
        {
            return &Options[i];
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintUsage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void PrintUsage(const char* Program, const char* Usage,
                       const ExampleOption* Options, int NumOptions)
{
    printf("%s\n\nUsage: %s [options]\n\n", Usage, Program);
    for (int i = 0; i < NumOptions; i++)
    {
        printf("  %-14s %s %s\n", Options[i].Name,
               Options[i].TakesValue ? "<value>" : "       ", Options[i].Help);
    }
    printf("  %-14s         Prints this text\n", "--help");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_CheckArguments -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool Example_CheckArguments(int Argc, char** Argv, const char* Usage,
                            const ExampleOption* Options, int NumOptions)
{
    for (int i = 1; i < Argc; i++)
    {
        if (strcmp(Argv[i], "--help") == 0)
        {
            PrintUsage(Argv[0], Usage, Options, NumOptions);
            return false;
        }
        const ExampleOption* Option = FindOption(Argv[i], Options, NumOptions);
        if (Option == NULL)
        {
            printf("Unknown option: %s; --help lists the options\n", Argv[i]);
            return false;
        }
        if (Option->TakesValue)
        {
            if (i + 1 >= Argc)
            {
                printf("Option %s needs a value\n", Argv[i]);
                return false;
            }
            i++;
        }
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_Failed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int Example_Failed(const char* What, DtNmosResult Result)
{
    printf("%s: %s: %s\n", What, DtNmosResult_Name(Result), DtNmos_GetLastError());
    return EXAMPLE_FAILED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_FindRegistry -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The list comes sorted, the usable registries first, in the order of their priority.
//
int Example_FindRegistry(DtNmosService Service, char* Url, size_t Size)
{
    DtNmosDiscoveryConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Size = sizeof(Config);
    Config.Service = Service;
    Config.TimeoutMs = 2000;
    Config.Log = Example_Log;
    DtNmosRegistryList* List = NULL;
    const DtNmosResult Result = DtNmos_Discover(&Config, &List);
    if (Result != DTNMOS_OK)
    {
        return Example_Failed("DtNmos_Discover", Result);
    }
    const DtNmosRegistryInfo* First = DtNmosRegistryList_At(List, 0);
    if (First == NULL || !First->Usable)
    {
        printf("No registry announces itself with DNS-SD; give one with --registry\n");
        DtNmosRegistryList_Free(List);
        return EXAMPLE_NOTHING;
    }
    printf("Found registry %s at %s\n", First->Instance, First->Url);
    snprintf(Url, Size, "%s", First->Url);
    DtNmosRegistryList_Free(List);
    return EXAMPLE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_HasFlag -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool Example_HasFlag(int Argc, char** Argv, const char* Name)
{
    for (int i = 1; i < Argc; i++)
    {
        if (strcmp(Argv[i], Name) == 0)
        {
            return true;
        }
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_Int64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool Example_Int64(int Argc, char** Argv, const char* Name, int64_t* Value)
{
    const char* Text = Example_Value(Argc, Argv, Name);
    if (Text == NULL)
    {
        return true;
    }
    char* End = NULL;
    errno = 0;
    const long long Parsed = strtoll(Text, &End, 10);
    if (End == Text || *End != '\0' || errno != 0)
    {
        printf("Option %s needs a number, not \"%s\"\n", Name, Text);
        return false;
    }
    *Value = (int64_t)Parsed;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_Log -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void Example_Log(void* User, DtNmosLogLevel Level, const char* Message)
{
    static const char* const Names[] = {"debug", "info", "warning", "error"};
    if (Level >= DTNMOS_LOG_WARNING || User != NULL)
    {
        fprintf(stderr, "[%s] %s\n", Names[Level], Message);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_ReadFile -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool Example_ReadFile(const char* Path, char** Text, size_t* Length)
{
    FILE* File = fopen(Path, "rb");
    if (File == NULL)
    {
        printf("Cannot open %s\n", Path);
        return false;
    }
    size_t Capacity = 4096;
    size_t Used = 0;
    char* Data = malloc(Capacity);
    while (Data != NULL)
    {
        Used += fread(Data + Used, 1, Capacity - Used - 1, File);
        if (Used + 1 < Capacity)
        {
            break;
        }
        Capacity *= 2;
        char* Grown = realloc(Data, Capacity);
        if (Grown == NULL)
        {
            free(Data);
        }
        Data = Grown;
    }
    const bool Failed = Data == NULL || ferror(File);
    fclose(File);
    if (Failed)
    {
        printf("Cannot read %s\n", Path);
        free(Data);
        return false;
    }
    Data[Used] = '\0';
    *Text = Data;
    *Length = Used;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_SleepMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void Example_SleepMs(int Ms)
{
#ifdef _WIN32
    Sleep((DWORD)Ms);
#else
    struct timespec Time = {.tv_sec = Ms / 1000, .tv_nsec = (long)(Ms % 1000) * 1000000L};
    nanosleep(&Time, NULL);
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Example_Value -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* Example_Value(int Argc, char** Argv, const char* Name)
{
    for (int i = 1; i + 1 < Argc; i++)
    {
        if (strcmp(Argv[i], Name) == 0)
        {
            return Argv[i + 1];
        }
    }
    return NULL;
}
