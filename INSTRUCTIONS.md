# dualLooper — Build & Flash Instructions (Daisy Seed + VS Code)

---

## 1. Prerequisites

Install these tools before anything else.

| Tool | Download |
|---|---|
| **ARM GCC toolchain** | https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads — get the `arm-none-eabi` package for your OS |
| **Make** | macOS: `xcode-select --install` · Linux: `sudo apt install make` · Windows: install via [Chocolatey](https://chocolatey.org/) → `choco install make` |
| **Git** | https://git-scm.com |
| **VS Code** | https://code.visualstudio.com |
| **dfu-util** | macOS: `brew install dfu-util` · Linux: `sudo apt install dfu-util` · Windows: download from http://dfu-util.sourceforge.net |

> **Windows users:** use [Git Bash](https://gitforwindows.org/) or WSL2 as your terminal — the Daisy Makefile system requires a Unix-like shell.

---

## 2. Folder structure

Clone the Daisy libraries **next to** your project folder:

```
workspace/
  libDaisy/        ← https://github.com/electro-smith/libDaisy
  DaisySP/         ← https://github.com/electro-smith/DaisySP
  dualLooper/      ← your project (this folder)
    dualLooper.cpp
    varSpeedLooper.h
    dsp.h
    Makefile
```

Clone the libraries:

```bash
git clone https://github.com/electro-smith/libDaisy.git
git clone https://github.com/electro-smith/DaisySP.git
```

---

## 3. Build the libraries (one time only)

```bash
# Build libDaisy
cd libDaisy
make -j4

# Build DaisySP
cd ../DaisySP
make -j4
```

---

## 4. Open the project in VS Code

1. Open VS Code.
2. **File → Open Folder** → select your `dualLooper/` folder.
3. Install the **C/C++** extension by Microsoft (if not already installed).

---

## 5. Configure VS Code tasks (build & flash)

Create the file `.vscode/tasks.json` inside your project folder with this content:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Build",
      "type": "shell",
      "command": "make -j4",
      "group": {
        "kind": "build",
        "isDefault": true
      },
      "problemMatcher": ["$gcc"]
    },
    {
      "label": "Clean",
      "type": "shell",
      "command": "make clean",
      "problemMatcher": []
    },
    {
      "label": "Flash (DFU)",
      "type": "shell",
      "command": "make program-dfu",
      "problemMatcher": [],
      "dependsOn": "Build"
    }
  ]
}
```

- **Build** → `Ctrl+Shift+B`
- **Flash** → `Ctrl+Shift+P` → *Tasks: Run Task* → *Flash (DFU)*

---

## 6. Build

Press **Ctrl+Shift+B** (or run `make -j4` in the terminal).

A successful build produces:
```
build/dualLooper.bin
build/dualLooper.elf
```

---

## 7. Flash to Daisy Seed via USB (DFU mode)

### Enter DFU (bootloader) mode on the Daisy Seed

1. Hold the **BOOT** button on the Daisy Seed.
2. While holding BOOT, press and release the **RESET** button.
3. Release BOOT. The board is now in DFU mode (no LED activity).

### Flash

**Option A — via VS Code task:**
Run the *Flash (DFU)* task (`Ctrl+Shift+P` → *Tasks: Run Task* → *Flash (DFU)*).

**Option B — manual terminal:**
```bash
make program-dfu
```

**Option C — Daisy Web Programmer (easiest, no drivers needed):**
1. Go to https://flash.daisy.audio in Chrome or Edge.
2. Enter DFU mode as above.
3. Click **Connect**, select the Daisy device.
4. Click **Choose File** and select `build/dualLooper.bin`.
5. Click **Program**.

After flashing, press **RESET** once to start running your code.

---

## 8. Verify it's working

- Plug audio into the Left input (Loop A) and Right input (Loop B).
- Connect headphones or a mixer to the Left and Right outputs.
- Both loops are mixed to both outputs (stereo).
- Tap footswitch A once → starts recording Loop A (LED A pulses).
- Tap again → stops recording and starts playback (LED A solid).
- Double-tap → pauses (LED A blinks).
- Hold 1 second → clears loop (LED A off).

---

## 9. Troubleshooting

| Problem | Fix |
|---|---|
| `arm-none-eabi-gcc: not found` | Add the toolchain `bin/` folder to your `PATH` |
| `make: command not found` (Windows) | Use Git Bash or install Make via Chocolatey |
| DFU device not found | Try a different USB cable (must be data, not charge-only); re-enter DFU mode |
| `libDaisy` build errors | Run `git submodule update --init` inside `libDaisy/` |
| Linker errors about `varSpeedLooper` | Make sure `varSpeedLooper.h` and `dsp.h` are in the same folder as `dualLooper.cpp` |
