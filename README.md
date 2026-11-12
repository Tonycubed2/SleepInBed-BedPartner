# Sleep In Bed SKSE - Bed Partner

A small SKSE patch for [Sleep In Bed SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/189803) by Elzar125.
It does **not** replace that mod: it requires it, and only changes who lies down in your bed.

## What it does

With Sleep In Bed's **Followers share the player's bed** on, the original always gives the free side of your bed
to the nearest follower. This patch adds a choice:

| Choice | Who may share your bed |
|---|---|
| Any follower | every follower |
| Opposite sex | only followers whose sex is not your character's |
| Same sex | only followers of your character's sex |

When more than one follower qualifies, one is picked **at random** each time you lie down.
Followers who are not picked, or do not qualify, go to other free beds nearby exactly as before.
A spouse who shares your bed is not affected.

Set it in **SKSE Menu → Sleep In Bed → Bed Partner** (needs SKSE Menu Framework), or in
`Data\SKSE\Plugins\SleepInBedPartner.ini` (`iBedPartner` = 0 any, 1 opposite sex, 2 same sex).

## Requirements

- Skyrim SE / AE with SKSE and Address Library
- **Sleep In Bed SKSE 1.4** (Nexus 189803): this exact version. On any other version the patch changes nothing,
  logs why, and the menu page says so.
- SKSE Menu Framework (optional, for the menu page)

## How it works

The patch hooks three functions inside `SleepInBedSKSE.dll` 1.4, after checking the DLL's build stamp and the
code bytes at each address:

- the bedtime function that sends followers to bed (to know which bed you lie in);
- the sort of nearby followers by distance (matching followers are moved to the front, in random order);
- the "may this follower use this bed" check (answers no for your bed to followers that do not match).

Log: `Documents\My Games\Skyrim Special Edition\SKSE\SleepInBedPartner.log`

## Build

Developer Command Prompt for VS 2022, one line at a time:

```
cd /d C:\dllmaker\SleepInBedPartner
cmake --preset vs2022-windows
cmake --build build --config Release
```

Result: `build\Release\SleepInBedPartner.dll`

## Credits and license

- Sleep In Bed SKSE by Elzar125 (GPL-3.0, open permissions)
- SKSE Menu Framework header (MIT, see THIRD-PARTY-SKSEMenuFramework-LICENSE.txt)
- This patch: GPL-3.0 (see LICENSE)
