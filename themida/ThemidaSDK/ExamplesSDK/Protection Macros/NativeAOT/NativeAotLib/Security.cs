using System.Runtime.InteropServices;

namespace NativeAotLib;

public static class Security
{
    [UnmanagedCallersOnly(EntryPoint = "CheckPassword")]
    public static bool CheckPassword(IntPtr passwordPtr)
    {
        SecureEngineSDK.VM_TIGER_WHITE_START();
        var password = Marshal.PtrToStringUni(passwordPtr);
        var result = password == "testpassword";
        SecureEngineSDK.VM_TIGER_WHITE_END();
        return result;
    }
    
    [UnmanagedCallersOnly(EntryPoint = "CheckPassword2")]
    public static bool CheckPassword2(IntPtr passwordPtr)
    {
        var result = Task.Run(async () => {
            SecureEngineSDK.VM_START();
            var password = Marshal.PtrToStringUni(passwordPtr);

            var correctPassword = (await File.ReadAllLinesAsync("password.txt")).First();

            var result = password == correctPassword;
            SecureEngineSDK.VM_END();
            return result;
        }).Result;
        
        return result;
    }
}