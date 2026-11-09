#pragma once
// Internal helper shared by UsdModelLoader.cpp and UsdSceneExporter.cpp (the only two TUs that include
// pxr). Registers the vcpkg USD plugins exactly once.
//
// vcpkg's USD layout has the per-plugin resources at <dir>/<plugin>/resources/plugInfo.json with no
// top-level aggregator, so we register those files individually rather than handing PlugRegistry a dir.
//
// Search order makes the deployed _Bin/<Config> self-contained AND keeps running-from-the-build-tree
// working: prefer a 'usd/' tree sitting next to the executable (CMake POST_BUILD copies it there), then
// fall back to the compile-time RANIM_USD_PLUGIN_DIR that points into the vcpkg install tree.

#include <filesystem>
#include <string>
#include <vector>

#include <pxr/base/plug/registry.h>

namespace RAnimation
{
    namespace detail
    {
#ifdef _WIN32
        // Forward-declared to avoid pulling <windows.h> (and its macros) into these pxr translation units.
        extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void*, char*, unsigned long);
#endif

        inline std::filesystem::path ExecutableDir()
        {
#ifdef _WIN32
            char buf[1024];
            const unsigned long n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
            if (n > 0 && n < sizeof(buf))
            {
                return std::filesystem::path(buf).parent_path();
            }
#endif
            return {};
        }

        inline std::vector<std::string> CollectUsdPlugInfos(const std::filesystem::path& dir)
        {
            std::vector<std::string> infos;
            std::error_code ec;
            for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            {
                const std::filesystem::path plugInfo = it->path() / "resources" / "plugInfo.json";
                if (std::filesystem::exists(plugInfo))
                {
                    infos.push_back(plugInfo.generic_string());
                }
            }
            return infos;
        }

        inline void RegisterUsdPluginsOnce()
        {
            static bool registered = false;
            if (registered)
            {
                return;
            }
            registered = true;

            std::vector<std::string> infos;

            std::error_code ec;
            const std::filesystem::path exeUsd = ExecutableDir() / "usd";
            if (!exeUsd.empty() && std::filesystem::exists(exeUsd, ec))
            {
                infos = CollectUsdPlugInfos(exeUsd);
            }

#ifdef RANIM_USD_PLUGIN_DIR
            if (infos.empty())
            {
                infos = CollectUsdPlugInfos(RANIM_USD_PLUGIN_DIR);
            }
#endif

            if (!infos.empty())
            {
                PXR_NS::PlugRegistry::GetInstance().RegisterPlugins(infos);
            }
        }
    } // namespace detail
} // namespace RAnimation
