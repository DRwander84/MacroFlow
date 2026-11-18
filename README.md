<div align="center">

<img src="resources/icons/logo.png" alt="MacroFlow logo" width="144" />

<h1>MacroFlow</h1>

<p><strong>A lightweight Windows macro recorder for mouse and keyboard actions.</strong></p>

<p>Built with C++20, Qt 6 and CMake.</p>

</div>

---

## Project overview

MacroFlow is organized into focused modules. The application layer starts the program and coordinates its main components; the core module handles recording and playback logic; Windows-specific code manages input hooks, global shortcuts and operating-system integration; and the UI module provides the desktop interface.

## Project structure

```text
MacroFlow/
├── app/                 # Application entry point and lifecycle
├── core/                # Macro events, recording, playback and controllers
├── platform/windows/    # Windows input hooks, mouse input and system integration
├── settings/            # Application settings and shortcut management
├── storage/             # Local database and macro repository
├── ui/                  # Main window, widgets, dialogs and visual state
├── resources/            # Icons, stylesheets and Qt resources
├── tests/                # Automated tests for core functionality
├── docs/                 # Architecture and project documentation
├── scripts/              # Build, Qt deployment and packaging scripts
├── CMakeLists.txt
├── LICENSE
├── NOTICE
└── README.md
```

## Build and package from source

The repository includes a single packaging script that builds the application, deploys the required Qt runtime files, runs the configured tests, and produces both a portable ZIP and a Windows installer.

### Requirements

Use the following toolchain for the current packaging scripts:

- Windows 10 or 11 (64-bit).
- Visual Studio 2026 with the **Desktop development with C++** workload and x64 C++ tools.
- CMake 4.4.5 or a version that supports the **Visual Studio 18 2026** generator.
- Qt 6.12.0, **MSVC 2022 64-bit** kit (`msvc2022_64`), with the Qt modules required by the project.
- Inno Setup 6.

> The current `scripts/build.ps1` selects the Visual Studio 2026 generator. The Qt kit path must point to the MSVC 2022 64-bit kit used by this project. Other compiler/Qt combinations may require changing the build configuration and have not been established by this packaging setup.

### Create both packages

1. Install the requirements above.
2. Clone or download the repository.
3. Open PowerShell or Command Prompt in the repository's root directory.
4. Run the command below, replacing the two paths with the locations on your own computer:

```powershell
.\scripts\package.bat -QtDir "C:\Path\To\Qt\6.12.0\msvc2022_64" -InnoDir "C:\Path\To\Inno Setup 6"
```

`-QtDir` must point to the Qt kit directory that contains `bin\windeployqt.exe`.

`-InnoDir` can point to the Inno Setup 6 installation directory or directly to `ISCC.exe`.

Do not copy the example paths literally; use the actual installation paths on your machine. Run the command from the repository root.

### Generated files

After a successful run, the following files are created:

```text
out/
├── MacroFlow-Portable.zip
├── portable/
│   └── MacroFlow/          # Extracted portable application files
└── setup/
    └── MacroFlow-Setup.exe
```

The portable ZIP can be extracted to a folder and launched using `MacroFlow.exe`. The Setup executable installs the application using the installer configuration in `scripts/MacroFlow.iss`.

## License

MacroFlow is distributed under the [Apache License, Version 2.0](LICENSE).

See [NOTICE](NOTICE) for attribution and third-party component information. Third-party dependencies retain their own license terms.
