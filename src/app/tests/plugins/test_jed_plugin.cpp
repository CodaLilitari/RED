// Synthetic JED plug-ins for the Plugins menu tests. The behaviour is chosen
// at build time with RED_TEST_JED_PLUGIN_MODE; each mode is a separate module.

#include "sed/plugins/jed_plugin_abi.hpp"

#include <cstdlib>

#if defined(_WIN32)
#define RED_TEST_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define RED_TEST_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define RED_TEST_PLUGIN_EDIT 1
#define RED_TEST_PLUGIN_FAIL 2
#define RED_TEST_PLUGIN_NO_ENTRY 3
#define RED_TEST_PLUGIN_SELECT 4
#define RED_TEST_PLUGIN_SED_ONLY 5
#define RED_TEST_PLUGIN_CRASH 6

namespace jed = sed::plugins::jed;

#if RED_TEST_JED_PLUGIN_MODE == RED_TEST_PLUGIN_EDIT
RED_TEST_PLUGIN_EXPORT bool JEDPluginLoad(jed::IJED* host)
{
    char label[] = "Test lights";
    char text[] = "test plug-in added two lights";
    host->StartUndo(label);
    jed::IJEDLevel* level = host->GetLevel();
    const int first = level->AddLight();
    const int second = level->AddLight();
    host->PanMessage(jed::msg_info, text);
    return first >= 0 && second == first + 1;
}
#elif RED_TEST_JED_PLUGIN_MODE == RED_TEST_PLUGIN_FAIL
RED_TEST_PLUGIN_EXPORT bool JEDPluginLoad(jed::IJED*)
{
    return false;
}
#elif RED_TEST_JED_PLUGIN_MODE == RED_TEST_PLUGIN_NO_ENTRY
RED_TEST_PLUGIN_EXPORT int redTestNotAPlugin()
{
    return 0;
}
#elif RED_TEST_JED_PLUGIN_MODE == RED_TEST_PLUGIN_SELECT
RED_TEST_PLUGIN_EXPORT bool JEDPluginLoad(jed::IJED* host)
{
    host->SetMapMode(jed::MM_LT);
    return host->SelectLT(0) >= 0;
}
#elif RED_TEST_JED_PLUGIN_MODE == RED_TEST_PLUGIN_SED_ONLY
RED_TEST_PLUGIN_EXPORT bool SEDPluginLoad(void*)
{
    return true;
}
#elif RED_TEST_JED_PLUGIN_MODE == RED_TEST_PLUGIN_CRASH
// Commits one edit, then ends the process abruptly. Only the out-of-process
// bridge tests load this module.
RED_TEST_PLUGIN_EXPORT bool JEDPluginLoad(jed::IJED* host)
{
    char label[] = "Crash light";
    host->StartUndo(label);
    host->GetLevel()->AddLight();
    host->UpdateMap();
    std::_Exit(3);
}
#else
#error "RED_TEST_JED_PLUGIN_MODE must select a test plug-in variant"
#endif
