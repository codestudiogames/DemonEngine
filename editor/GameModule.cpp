#include <windows.h>
#include <cstdint>

extern "C" __declspec(dllexport) uint32_t DemonGameModuleVersion()
{
    return 1;
}

extern "C" __declspec(dllexport) const char* DemonGameModuleApi()
{
    return "DemonEngine.GameModule/1";
}

extern "C" __declspec(dllexport) void DemonGameModuleStartup()
{
}

extern "C" __declspec(dllexport) void DemonGameModuleShutdown()
{
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
    return TRUE;
}
