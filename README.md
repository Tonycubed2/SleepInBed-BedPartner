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

**Third person while in bed** (optional, off by default): while you lie in bed the camera stays in third person,
so you can look around the room. If anything switches it to first person, it is switched back and the log says when.

**After sleeping** (both on by default = Sleep In Bed's normal behavior): "Put my gear back on" and
"Put followers' gear back on". Turn one off and that person stays as they are when they get up
(in sleepwear or undressed); Sleep In Bed still forgets the gear it took off, so the next bedtime works as usual.

**Menu key fix:** Sleep In Bed 1.4 claims its sleep key (by default the Wait key / controller Back button)
everywhere, so in unpaused inventory, magic or favorites menus that button opened the Wait menu instead of
switching menus. With this patch the key is only claimed when Sleep In Bed uses it (lying in bed, or a jail
sentence to serve, with no menu open); everywhere else the game's own handling applies.

**Eyes stay closed after sleeping (fixed):** Sleep In Bed 1.4 reopens the eyes once, at the moment the
sleeper starts getting up, and never checks again. If the eyes close again during the get-up animation they
stay closed until a reload. The patch watches you and the followers who slept while getting up and for 8 s
after standing; eyes still fully closed for longer than a blink are opened again (logged).

**Clear the room and close the door** (optional, off by default; like the old Go To Bed addon): when you lie
down indoors, the nearest ordinary door to your bed is taken as the room door. Other NPCs on your side of it walk
out, then a follower (your bed partner, else another follower going to bed) walks to the door, closes it and goes
to bed. If the door is opened again while you are in bed, she gets up and closes it again. Everything is released
when you get up.

**Teammates who are not followers:** some mods flag an NPC as your teammate before they really follow you
(Vilja before her quest is accepted). A teammate who is not a member of the game's follower faction
(CurrentFollowerFaction) is ignored: never sent to bed, never chosen for your bed; with "Clear the room" they
leave the room like other NPCs. Once they become real followers, they count again. Extra names can be listed
in `sNotFollowers` (comma separated).

Set it in **SKSE Menu → Sleep In Bed → Bed Partner** (needs SKSE Menu Framework), or in
`Data\SKSE\Plugins\SleepInBedPartner.ini` (`iBedPartner` = 0 any, 1 opposite sex, 2 same sex; `bThirdPersonInBed` = 0/1; `bRedressPlayer`, `bRedressFollowers`, `bClearRoom` = 0/1).

## Requirements

- Skyrim SE / AE / VR with SKSE (SKSEVR for VR) and Address Library (VR Address Library for VR).
  One DLL for all three. In VR the "Third person while in bed" option is not available.
- **Sleep In Bed SKSE 1.4** (Nexus 189803): this exact version. On any other version the patch changes nothing,
  logs why, and the menu page says so.
- SKSE Menu Framework (optional, for the menu page)

## How it works

The patch hooks three functions inside `SleepInBedSKSE.dll` 1.4, after checking the DLL's build stamp and the
code bytes at each address:

- the bedtime function that sends followers to bed (to know which bed you lie in);
- the sort of nearby followers by distance (matching followers are moved to the front, in random order);
- the "may this follower use this bed" check (answers no for your bed to followers that do not match);
- the "send this follower to this bed" function (to know who was sent to your bed, and to send the next one).

- Sleep In Bed's own guard on package selection (to also protect followers while they walk to bed).

**Why some followers never arrive (fixed):** Sleep In Bed protects its sleep package only once a follower is
lying down. While she walks to the bed, any package re-evaluation picks her quest follow package instead
(Skyrim.esm 0005C84B, used by CHIM's HerikaFollower quest and the vanilla DialogueFollower quest), which
outranks the sleep package. Sleep In Bed resends it every ~3 s, up to 5 times, then gives up. While you lie in
bed, this patch keeps the sleep package during the walk too. When you get up, everything is released as before.

As a safety net, the patch also watches the follower sent to your bed: if she still loses her sleep package
twice, or never starts it, the next matching follower is sent to your bed instead.

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
