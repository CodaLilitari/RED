# JED Light Baker 64-bit build patch

[JED Light Baker](https://github.com/LadyEebs/JED-Light-Baker) by LadyEebs is a JED lighting plug-in. Its published
release is a 32-bit DLL, and RED's plug-in host refuses DLLs whose architecture differs from the running editor (see
the Plug-ins section of `docs/user_guide.md`). A 64-bit RED therefore needs a 64-bit build of the plug-in.

This directory keeps only RED's changes as a patch series. The Light Baker source is not vendored here. The applied
series is maintained in the [CodaLilitari/JED-Light-Baker](https://github.com/CodaLilitari/JED-Light-Baker) fork.

The patches:

1. add an `x64` configuration to the Visual Studio solution and project, make the plug-in source pointer-size safe
   for a 64-bit host, and add a GitHub Actions workflow that builds the x64 Release DLL
2. write Debug x64 shader output to `Source/*.cso`, where the resource script embeds it, and ignore `Source/x64`
3. build CI with the runner's native toolset (forcing v143 on the Visual Studio 2026 runner fails with `MSB8041`
   because its MFC libraries are not installed) and install MFC if it is missing
4. compile `BakeAmbient.hlsl` as a compute shader in the x64 configurations (without this, it compiled as `vs_2_0`
   and failed with `X3701`)
5. document the 64-bit build in the plug-in README

## Status

The x64 Release DLL builds on the GitHub `windows-latest` runner (Visual Studio 2026, v145 toolset). CI confirms the
DLL is x64 and exports `JEDPluginLoadStdCall`. It has not yet been loaded and tested inside RED.

## Use

Either download the `light-baker-x64` artifact from the fork's `Build x64` workflow, or build it yourself:

```powershell
git clone https://github.com/LadyEebs/JED-Light-Baker
cd JED-Light-Baker
git checkout c52346951e6c6a8e60d3f034946bedefe52f4f25
git am <RED checkout>/tools/jed-light-baker-x64/*.patch
msbuild "Source/Light Baker.sln" /p:Configuration=Release /p:Platform=x64
```

The build needs Visual Studio's "C++ MFC for latest build tools" component. Copy `Source/x64/Release/Light Baker.dll`
into the `plugins` folder beside the RED executable, then use **Plugins ▸ Rescan**.

## Licensing

The upstream repository does not publish a license. Ask the author for permission before redistributing builds of the
plug-in.
