# Sonic Robo Blast 2 × Super Mario 64 (YES, THIS IS VIBE-CODED)

![SRB2 × SM64](Gemini_Generated_Image_s7lfges7lfges7lf.png)

> **⚠ Experimental / Beta**  
> This is a work in progress. Expect bugs, broken interactions, and missing polish. It is likely not possible to complete the game using Mario — many mechanics are untested or partially implemented. Use at your own risk.

This project integrates [libsm64](https://github.com/libsm64/libsm64) directly into the [Sonic Robo Blast 2](https://srb2.org/) source code, letting you play SRB2 as Mario with faithful N64 physics, animations, and mechanics.

Mario interacts natively with the SRB2 world:
- Triple jumps, long jumps, ground pounds, and backflips behave as in the original SM64
- SRB2 springs launch Mario with geometrically correct trajectories, including diagonal spring handling tuned for maps like Deep Sea Zone
- Ring loss, death, and damage are synchronized between the SM64 runtime and SRB2's Doom-format systems

---

## Building (MSYS2 / MinGW-w64 on Windows)

### 1. Set up the environment

1. Download and install [MSYS2](https://www.msys2.org/).
2. Open the **MSYS2 MinGW 64-bit** terminal.
3. Install the default MinGW64 toolchain.

### 2. Set path variables

Export the local directories so temporary files don't hit permission errors under `/tmp`:

```bash
export PATH=/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH
export TMPDIR=/tmp
export TMP=/tmp
export TEMP=/tmp
```

### 3. Build

Navigate to the repository folder:

```bash
cd /c/Users/YourName/path/to/SRB2
```

Build with parallel jobs (adjust `-j4` to match your CPU core count):

```bash
make MINGW64=1 -j4
```

### 4. Provide the SM64 ROM

libsm64 extracts Mario's assets (textures, animations, sounds) directly from the original ROM at runtime. You need a **Super Mario 64 (USA) ROM** — the exact file the library expects is:

```
baserom.us.z64
```

Place it in the same folder as `srb2win64.exe` before launching.

> **Note:** We cannot distribute the ROM. You must obtain it yourself from a copy of the game you own.

### 5. Run

A successful build produces `srb2win64.exe` in the `bin/` folder. Launch it with the ROM in the same directory, then use the `sm64_enable 1` console command to activate Mario physics.

---

*Sonic Team Junior is in no way affiliated with SEGA or Sonic Team. We do not claim ownership of any of SEGA's intellectual property used in SRB2.*  
*Super Mario and Super Mario 64 are trademarks of Nintendo. This project is a non-profit fan project built on libsm64.*
