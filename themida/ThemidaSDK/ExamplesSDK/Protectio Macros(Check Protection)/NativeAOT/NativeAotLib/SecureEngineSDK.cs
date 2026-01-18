// ******************************************************************************
// Header: SecureEngineSDK.cs
// Description: .NET Native AOT macros definitions
//
// Author/s: Oreans Technologies 
// (c) 2023 Oreans Technologies
//
// --- File generated automatically from Oreans VM Generator (31/8/2023) ---
// ******************************************************************************

using System.Runtime.InteropServices;

public static class SecureEngineSDK {

    #if WIN32

        public const string SECUREENGINE_SDK_DLL = "SecureEngineSDK.dll";

        public const string VM_TIGER_WHITE_START_ENTRYPOINT = "CustomVM00000100_Start";
        public const string VM_TIGER_WHITE_END_ENTRYPOINT = "CustomVM00000100_End";
        public const string VM_TIGER_RED_START_ENTRYPOINT = "CustomVM00000101_Start";
        public const string VM_TIGER_RED_END_ENTRYPOINT = "CustomVM00000101_End";
        public const string VM_TIGER_BLACK_START_ENTRYPOINT = "CustomVM00000102_Start";
        public const string VM_TIGER_BLACK_END_ENTRYPOINT = "CustomVM00000102_End";
        public const string VM_FISH_WHITE_START_ENTRYPOINT = "CustomVM00000106_Start";
        public const string VM_FISH_WHITE_END_ENTRYPOINT = "CustomVM00000106_End";
        public const string VM_FISH_RED_START_ENTRYPOINT = "CustomVM00000108_Start";
        public const string VM_FISH_RED_END_ENTRYPOINT = "CustomVM00000108_End";
        public const string VM_FISH_BLACK_START_ENTRYPOINT = "CustomVM00000110_Start";
        public const string VM_FISH_BLACK_END_ENTRYPOINT = "CustomVM00000110_End";
        public const string VM_PUMA_WHITE_START_ENTRYPOINT = "CustomVM00000112_Start";
        public const string VM_PUMA_WHITE_END_ENTRYPOINT = "CustomVM00000112_End";
        public const string VM_PUMA_RED_START_ENTRYPOINT = "CustomVM00000114_Start";
        public const string VM_PUMA_RED_END_ENTRYPOINT = "CustomVM00000114_End";
        public const string VM_PUMA_BLACK_START_ENTRYPOINT = "CustomVM00000116_Start";
        public const string VM_PUMA_BLACK_END_ENTRYPOINT = "CustomVM00000116_End";
        public const string VM_SHARK_WHITE_START_ENTRYPOINT = "CustomVM00000118_Start";
        public const string VM_SHARK_WHITE_END_ENTRYPOINT = "CustomVM00000118_End";
        public const string VM_SHARK_RED_START_ENTRYPOINT = "CustomVM00000120_Start";
        public const string VM_SHARK_RED_END_ENTRYPOINT = "CustomVM00000120_End";
        public const string VM_SHARK_BLACK_START_ENTRYPOINT = "CustomVM00000122_Start";
        public const string VM_SHARK_BLACK_END_ENTRYPOINT = "CustomVM00000122_End";
        public const string VM_DOLPHIN_WHITE_START_ENTRYPOINT = "CustomVM00000134_Start";
        public const string VM_DOLPHIN_WHITE_END_ENTRYPOINT = "CustomVM00000134_End";
        public const string VM_DOLPHIN_RED_START_ENTRYPOINT = "CustomVM00000136_Start";
        public const string VM_DOLPHIN_RED_END_ENTRYPOINT = "CustomVM00000136_End";
        public const string VM_DOLPHIN_BLACK_START_ENTRYPOINT = "CustomVM00000138_Start";
        public const string VM_DOLPHIN_BLACK_END_ENTRYPOINT = "CustomVM00000138_End";
        public const string VM_EAGLE_WHITE_START_ENTRYPOINT = "CustomVM00000146_Start";
        public const string VM_EAGLE_WHITE_END_ENTRYPOINT = "CustomVM00000146_End";
        public const string VM_EAGLE_RED_START_ENTRYPOINT = "CustomVM00000148_Start";
        public const string VM_EAGLE_RED_END_ENTRYPOINT = "CustomVM00000148_End";
        public const string VM_EAGLE_BLACK_START_ENTRYPOINT = "CustomVM00000150_Start";
        public const string VM_EAGLE_BLACK_END_ENTRYPOINT = "CustomVM00000150_End";
        public const string VM_LION_WHITE_START_ENTRYPOINT = "CustomVM00000160_Start";
        public const string VM_LION_WHITE_END_ENTRYPOINT = "CustomVM00000160_End";
        public const string VM_LION_RED_START_ENTRYPOINT = "CustomVM00000162_Start";
        public const string VM_LION_RED_END_ENTRYPOINT = "CustomVM00000162_End";
        public const string VM_LION_BLACK_START_ENTRYPOINT = "CustomVM00000164_Start";
        public const string VM_LION_BLACK_END_ENTRYPOINT = "CustomVM00000164_End";
        public const string VM_COBRA_WHITE_START_ENTRYPOINT = "CustomVM00000166_Start";
        public const string VM_COBRA_WHITE_END_ENTRYPOINT = "CustomVM00000166_End";
        public const string VM_COBRA_RED_START_ENTRYPOINT = "CustomVM00000168_Start";
        public const string VM_COBRA_RED_END_ENTRYPOINT = "CustomVM00000168_End";
        public const string VM_COBRA_BLACK_START_ENTRYPOINT = "CustomVM00000170_Start";
        public const string VM_COBRA_BLACK_END_ENTRYPOINT = "CustomVM00000170_End";
        public const string VM_WOLF_WHITE_START_ENTRYPOINT = "CustomVM00000172_Start";
        public const string VM_WOLF_WHITE_END_ENTRYPOINT = "CustomVM00000172_End";
        public const string VM_WOLF_RED_START_ENTRYPOINT = "CustomVM00000174_Start";
        public const string VM_WOLF_RED_END_ENTRYPOINT = "CustomVM00000174_End";
        public const string VM_WOLF_BLACK_START_ENTRYPOINT = "CustomVM00000176_Start";
        public const string VM_WOLF_BLACK_END_ENTRYPOINT = "CustomVM00000176_End";
        public const string VM_MUTATE_ONLY_START_ENTRYPOINT = "Mutate_Start";
        public const string VM_MUTATE_ONLY_END_ENTRYPOINT = "Mutate_End";
        public const string VM_FALCON_TINY_START_ENTRYPOINT = "CustomVM0000217_Start";
        public const string VM_FALCON_TINY_END_ENTRYPOINT = "CustomVM0000217_End";
    
#else

        public const string SECUREENGINE_SDK_DLL = "SecureEngineSDK64.dll";

        public const string VM_TIGER_WHITE_START_ENTRYPOINT = "CustomVM00000103_Start";
        public const string VM_TIGER_WHITE_END_ENTRYPOINT = "CustomVM00000103_End";
        public const string VM_TIGER_RED_START_ENTRYPOINT = "CustomVM00000104_Start";
        public const string VM_TIGER_RED_END_ENTRYPOINT = "CustomVM00000104_End";
        public const string VM_TIGER_BLACK_START_ENTRYPOINT = "CustomVM00000105_Start";
        public const string VM_TIGER_BLACK_END_ENTRYPOINT = "CustomVM00000105_End";
        public const string VM_FISH_WHITE_START_ENTRYPOINT = "CustomVM00000107_Start";
        public const string VM_FISH_WHITE_END_ENTRYPOINT = "CustomVM00000107_End";
        public const string VM_FISH_RED_START_ENTRYPOINT = "CustomVM00000109_Start";
        public const string VM_FISH_RED_END_ENTRYPOINT = "CustomVM00000109_End";
        public const string VM_FISH_BLACK_START_ENTRYPOINT = "CustomVM00000111_Start";
        public const string VM_FISH_BLACK_END_ENTRYPOINT = "CustomVM00000111_End";
        public const string VM_PUMA_WHITE_START_ENTRYPOINT = "CustomVM00000113_Start";
        public const string VM_PUMA_WHITE_END_ENTRYPOINT = "CustomVM00000113_End";
        public const string VM_PUMA_RED_START_ENTRYPOINT = "CustomVM00000115_Start";
        public const string VM_PUMA_RED_END_ENTRYPOINT = "CustomVM00000115_End";
        public const string VM_PUMA_BLACK_START_ENTRYPOINT = "CustomVM00000117_Start";
        public const string VM_PUMA_BLACK_END_ENTRYPOINT = "CustomVM00000117_End";
        public const string VM_SHARK_WHITE_START_ENTRYPOINT = "CustomVM00000119_Start";
        public const string VM_SHARK_WHITE_END_ENTRYPOINT = "CustomVM00000119_End";
        public const string VM_SHARK_RED_START_ENTRYPOINT = "CustomVM00000121_Start";
        public const string VM_SHARK_RED_END_ENTRYPOINT = "CustomVM00000121_End";
        public const string VM_SHARK_BLACK_START_ENTRYPOINT = "CustomVM00000123_Start";
        public const string VM_SHARK_BLACK_END_ENTRYPOINT = "CustomVM00000123_End";
        public const string VM_DOLPHIN_WHITE_START_ENTRYPOINT = "CustomVM00000135_Start";
        public const string VM_DOLPHIN_WHITE_END_ENTRYPOINT = "CustomVM00000135_End";
        public const string VM_DOLPHIN_RED_START_ENTRYPOINT = "CustomVM00000137_Start";
        public const string VM_DOLPHIN_RED_END_ENTRYPOINT = "CustomVM00000137_End";
        public const string VM_DOLPHIN_BLACK_START_ENTRYPOINT = "CustomVM00000139_Start";
        public const string VM_DOLPHIN_BLACK_END_ENTRYPOINT = "CustomVM00000139_End";
        public const string VM_EAGLE_WHITE_START_ENTRYPOINT = "CustomVM00000147_Start";
        public const string VM_EAGLE_WHITE_END_ENTRYPOINT = "CustomVM00000147_End";
        public const string VM_EAGLE_RED_START_ENTRYPOINT = "CustomVM00000149_Start";
        public const string VM_EAGLE_RED_END_ENTRYPOINT = "CustomVM00000149_End";
        public const string VM_EAGLE_BLACK_START_ENTRYPOINT = "CustomVM00000151_Start";
        public const string VM_EAGLE_BLACK_END_ENTRYPOINT = "CustomVM00000151_End";
        public const string VM_LION_WHITE_START_ENTRYPOINT = "CustomVM00000161_Start";
        public const string VM_LION_WHITE_END_ENTRYPOINT = "CustomVM00000161_End";
        public const string VM_LION_RED_START_ENTRYPOINT = "CustomVM00000163_Start";
        public const string VM_LION_RED_END_ENTRYPOINT = "CustomVM00000163_End";
        public const string VM_LION_BLACK_START_ENTRYPOINT = "CustomVM00000165_Start";
        public const string VM_LION_BLACK_END_ENTRYPOINT = "CustomVM00000165_End";
        public const string VM_COBRA_WHITE_START_ENTRYPOINT = "CustomVM00000167_Start";
        public const string VM_COBRA_WHITE_END_ENTRYPOINT = "CustomVM00000167_End";
        public const string VM_COBRA_RED_START_ENTRYPOINT = "CustomVM00000169_Start";
        public const string VM_COBRA_RED_END_ENTRYPOINT = "CustomVM00000169_End";
        public const string VM_COBRA_BLACK_START_ENTRYPOINT = "CustomVM00000171_Start";
        public const string VM_COBRA_BLACK_END_ENTRYPOINT = "CustomVM00000171_End";
        public const string VM_WOLF_WHITE_START_ENTRYPOINT = "CustomVM00000173_Start";
        public const string VM_WOLF_WHITE_END_ENTRYPOINT = "CustomVM00000173_End";
        public const string VM_WOLF_RED_START_ENTRYPOINT = "CustomVM00000175_Start";
        public const string VM_WOLF_RED_END_ENTRYPOINT = "CustomVM00000175_End";
        public const string VM_WOLF_BLACK_START_ENTRYPOINT = "CustomVM00000177_Start";
        public const string VM_WOLF_BLACK_END_ENTRYPOINT = "CustomVM00000177_End";
        public const string VM_MUTATE_ONLY_START_ENTRYPOINT = "Mutate_Start";
        public const string VM_MUTATE_ONLY_END_ENTRYPOINT = "Mutate_End";
        public const string VM_FALCON_TINY_START_ENTRYPOINT = "CustomVM0000218_Start";
        public const string VM_FALCON_TINY_END_ENTRYPOINT = "CustomVM0000218_End";
    
#endif

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_TIGER_WHITE_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_TIGER_WHITE_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_TIGER_WHITE_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_TIGER_WHITE_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_TIGER_RED_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_TIGER_RED_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_TIGER_RED_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_TIGER_RED_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_TIGER_BLACK_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_TIGER_BLACK_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_TIGER_BLACK_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_TIGER_BLACK_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_FISH_WHITE_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_FISH_WHITE_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_FISH_WHITE_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_FISH_WHITE_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_FISH_RED_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_FISH_RED_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_FISH_RED_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_FISH_RED_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_FISH_BLACK_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_FISH_BLACK_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_FISH_BLACK_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_FISH_BLACK_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_PUMA_WHITE_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_PUMA_WHITE_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_PUMA_WHITE_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_PUMA_WHITE_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_PUMA_RED_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_PUMA_RED_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_PUMA_RED_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_PUMA_RED_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_PUMA_BLACK_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_PUMA_BLACK_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_PUMA_BLACK_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_PUMA_BLACK_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_SHARK_WHITE_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_SHARK_WHITE_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_SHARK_WHITE_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_SHARK_WHITE_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_SHARK_RED_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_SHARK_RED_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_SHARK_RED_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_SHARK_RED_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_SHARK_BLACK_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_SHARK_BLACK_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_SHARK_BLACK_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_SHARK_BLACK_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_DOLPHIN_WHITE_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_DOLPHIN_WHITE_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_DOLPHIN_WHITE_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_DOLPHIN_WHITE_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_DOLPHIN_RED_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_DOLPHIN_RED_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_DOLPHIN_RED_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_DOLPHIN_RED_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_DOLPHIN_BLACK_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_DOLPHIN_BLACK_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_DOLPHIN_BLACK_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_DOLPHIN_BLACK_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_EAGLE_WHITE_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_EAGLE_WHITE_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_EAGLE_WHITE_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_EAGLE_WHITE_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_EAGLE_RED_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_EAGLE_RED_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_EAGLE_RED_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_EAGLE_RED_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_EAGLE_BLACK_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_EAGLE_BLACK_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_EAGLE_BLACK_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_EAGLE_BLACK_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_LION_WHITE_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_LION_WHITE_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_LION_WHITE_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_LION_WHITE_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_LION_RED_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_LION_RED_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_LION_RED_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_LION_RED_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_LION_BLACK_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_LION_BLACK_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_LION_BLACK_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_LION_BLACK_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_COBRA_WHITE_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_COBRA_WHITE_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_COBRA_WHITE_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_COBRA_WHITE_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_COBRA_RED_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_COBRA_RED_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_COBRA_RED_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_COBRA_RED_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_COBRA_BLACK_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_COBRA_BLACK_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_COBRA_BLACK_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_COBRA_BLACK_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_WOLF_WHITE_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_WOLF_WHITE_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_WOLF_WHITE_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_WOLF_WHITE_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_WOLF_RED_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_WOLF_RED_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_WOLF_RED_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_WOLF_RED_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_WOLF_BLACK_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_WOLF_BLACK_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_WOLF_BLACK_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_WOLF_BLACK_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_MUTATE_ONLY_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_MUTATE_ONLY_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_MUTATE_ONLY_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_MUTATE_ONLY_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_FALCON_TINY_START_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_FALCON_TINY_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = VM_FALCON_TINY_END_ENTRYPOINT, CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_FALCON_TINY_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "VMStart", CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "VMEnd", CallingConvention = CallingConvention.StdCall)]
    public static extern void VM_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "MutateStart", CallingConvention = CallingConvention.StdCall)]
    public static extern void MUTATE_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "MutateEnd", CallingConvention = CallingConvention.StdCall)]
    public static extern void MUTATE_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "RegisteredStart", CallingConvention = CallingConvention.StdCall)]
    public static extern void REGISTERED_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "RegisteredEnd", CallingConvention = CallingConvention.StdCall)]
    public static extern void REGISTERED_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "StrEncryptStart", CallingConvention = CallingConvention.StdCall)]
    public static extern void STR_ENCRYPT_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "StrEncryptEnd", CallingConvention = CallingConvention.StdCall)]
    public static extern void STR_ENCRYPT_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "StrEncryptWStart", CallingConvention = CallingConvention.StdCall)]
    public static extern void STR_ENCRYPTW_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "StrEncryptWEnd", CallingConvention = CallingConvention.StdCall)]
    public static extern void STR_ENCRYPTW_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "UnprotectedStart", CallingConvention = CallingConvention.StdCall)]
    public static extern void UNPROTECTED_START();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "UnprotectedEnd", CallingConvention = CallingConvention.StdCall)]
    public static extern void UNPROTECTED_END();

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "SECheckProtection", CallingConvention = CallingConvention.StdCall)]
    public static extern void CHECK_PROTECTION(ref int user_var, int user_value);

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "SECheckCodeIntegrity", CallingConvention = CallingConvention.StdCall)]
    public static extern void CHECK_CODE_INTEGRITY(ref int user_var, int user_value);

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "SECheckRegistration", CallingConvention = CallingConvention.StdCall)]
    public static extern void CHECK_REGISTRATION(ref int user_var, int user_value);

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "SECheckVirtualPC", CallingConvention = CallingConvention.StdCall)]
    public static extern void CHECK_VIRTUAL_PC(ref int user_var, int user_value);

    [DllImport(SECUREENGINE_SDK_DLL, EntryPoint = "SECheckDebugger", CallingConvention = CallingConvention.StdCall)]
    public static extern void CHECK_DEBUGGER(ref int user_var, int user_value);

}
