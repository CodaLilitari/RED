# JED Light Baker 64-bit build patch

[JED Light Baker](https://github.com/LadyEebs/JED-Light-Baker) by LadyEebs is a JED lighting plug-in. Its published
release is a 32-bit DLL, and RED's plug-in host refuses DLLs whose architecture differs from the running editor (see
the Plug-ins section of `docs/user_guide.md`). A 64-bit RED therefore needs a 64-bit build of the plug-in.

This directory keeps only RED's changes as a patch. The Light Baker source is not vendored here; fetch it from the
upstream repository.

The patch:

- adds an `x64` configuration to the Visual Studio solution and project
- makes the plug-in source pointer-size safe for a 64-bit host
- adds a GitHub Actions workflow that builds the x64 Release DLL

## Status

The patch has not been compiled or loaded in a 64-bit host. Treat it as a starting point that requires a Windows MSVC
build and an in-editor test.

## Use

```powershell
git clone https://github.com/LadyEebs/JED-Light-Baker
cd JED-Light-Baker
git checkout c52346951e6c6a8e60d3f034946bedefe52f4f25
git am <RED checkout>/tools/jed-light-baker-x64/0001-Build-a-64-bit-plugin-for-64-bit-JED-compatible-host.patch
msbuild "Source/Light Baker.sln" /p:Configuration=Release /p:Platform=x64
```

Copy the resulting DLL into the `plugins` folder beside the RED executable, then use **Plugins ▸ Rescan**.

## Licensing

The upstream repository does not publish a license. Ask the author for permission before redistributing builds of the
plug-in.
