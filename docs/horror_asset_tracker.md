# Horror Game Asset Tracker

This is the production checklist for the horror game described in the
[story treatment](<Psychological Horror Game. Story Treatment.md>). It tracks source assets,
authored game content, and engine work that must exist before each part of the game can be built.

The active asset root is `games/horror/assets/`. Paths in this document are relative to that
folder unless stated otherwise.

## Tracking legend

| Status | Meaning |
|---|---|
| ⬜ Not started | Nothing usable exists yet |
| 🟡 In development | A placeholder or unfinished version exists |
| ✅ Done | Imported, tested in the engine, and ready to use |
| ⏸ Blocked | Waiting for an engine feature, decision, or external asset |
| ❌ Cut | Deliberately removed from the current scope |

| Priority | Meaning |
|---|---|
| P0 | Required for the opening apartment vertical slice |
| P1 | Required for Act I |
| P2 | Required for the later full story |
| Optional | Only needed if the story keeps that location or feature |

Update the Status, Path, Source, and Notes cells whenever work changes. An asset is only Done when:

1. Its source and license are recorded.
2. Its final files exist under `games/horror/assets/`.
3. It loads without errors and has the correct scale and orientation.
4. Its materials and audio settings look or sound correct in the engine.
5. It has been placed or exercised in at least one scene.

## Technical delivery requirements

| Asset type | Preferred format | Requirements |
|---|---|---|
| Static model | glTF 2.0 `.gltf` | Y-up, meters, separate files for interactive objects |
| Animated model | glTF 2.0 with skin and animation | Blocked until skeletal animation is implemented |
| Texture | `.png` or `.jpg` | Albedo, normal, metallic, roughness, and AO are supported |
| HDR environment | `.hdr` | Equirectangular HDR used for skybox and IBL |
| Positional audio | `.wav` | Mono PCM, preferably 16-bit at 44.1 or 48 kHz |
| Music or ambience | `.wav` for now | OGG and streaming are not implemented yet |
| Scene | `.json` | Saved through the scene editor under `assets/scenes/` |
| Gameplay logic | `.lua` | Keep horror scripts inside `assets/scripts/` |
| UI image | `.png` | Use transparency where appropriate |

The material system has an entity-level emissive color and intensity override, but no emissive
texture slot. True mirrors, skeletal animation, audio streaming, and OGG playback are not
currently available.

## Current project foundation

| Status | Priority | Asset | Path | Source | Notes |
|---|---|---|---|---|---|
| ✅ Done | P0 | Horror executable target | `../Horror.exe` build target | Project | Builds from `games/horror/main.cpp` |
| ✅ Done | P0 | Game launch configuration | `game.json` | Project | Selects scene, HDR, frame cap, and editor/play behavior |
| 🟡 In development | P0 | Main horror scene | `scenes/horror.json` | Project | Asphalt material preview scene; apartment layout not started |
| ✅ Done | P0 | Audio folder | `audio/` | Project | Folder exists |
| ✅ Done | P0 | HDR folder | `hdr/` | Project | Folder exists |
| ✅ Done | P0 | Models folder | `models/` | Project | Folder exists |
| ✅ Done | P0 | Scripts folder | `scripts/` | Project | Folder exists |
| ✅ Done | P0 | Textures folder | `textures/` | Project | Folder exists |

## P0: Opening apartment vertical slice

The first playable target ends after the opening apartment sequence and one dark-figure encounter.

| Status | Asset | Type | Suggested path | Source | Notes |
|---|---|---|---|---|---|
| ⬜ Not started | Apartment floor plan and scale reference | Reference | `references/apartment/` |  | Choose country, building age, and room dimensions first |
| ⬜ Not started | Apartment shell | Static model | `models/apartment/apartment_shell.gltf` |  | Bedroom, living room, kitchen, bathroom, hall, entrance |
| ⬜ Not started | Apartment collision layout | Scene content | `scenes/apartment.json` | Project | Box colliders authored in the scene |
| ⬜ Not started | Front door | Static model | `models/apartment/front_door.gltf` |  | Separate pivot for scripted opening |
| ⬜ Not started | Keys and keychain | Static model | `models/props/keys.gltf` |  | Must be movable or hideable by script |
| ⬜ Not started | Alarm clock or phone alarm | Static model | `models/props/alarm_clock.gltf` |  | Place beside bed |
| ⬜ Not started | Bed and bedding | Static model | `models/furniture/bed.gltf` |  | Main opening view |
| ⬜ Not started | Refrigerator | Static model | `models/apartment/refrigerator.gltf` |  | Source of recurring room sound |
| ⬜ Not started | Childhood drawing | Texture and plane | `textures/story/doctor_drawing.png` | Project | “I want to help people” visual callback |
| ⬜ Not started | School certificate | Texture and plane | `textures/story/school_certificate.png` | Project | Establishes lost potential |
| ⬜ Not started | Biology or medical textbook | Static model | `models/props/biology_textbook.gltf` |  | Story prop |
| ⬜ Not started | Empty bottles and cans set | Static models | `models/props/bottles/` |  | Use restrained quantities rather than caricature |
| ⬜ Not started | Clothes and rubbish set | Static models | `models/props/clutter/` |  | Lived-in apartment dressing |
| ⬜ Not started | One static dark figure | Static model | `models/figures/dark_figure_standing.gltf` |  | No animation required for the first encounter |
| ⬜ Not started | First-person controller script | Lua | `scripts/fps_controller.lua` | Project | Horror-owned copy, not shared with Testbed |
| ⬜ Not started | Opening sequence director | Lua | `scripts/opening_sequence.lua` | Project | Controls wake-up, voices, keys, and sequence state |
| ⬜ Not started | Key-search interaction | Lua | `scripts/keys.lua` | Project | Keys appear after the player checks expected locations |
| ⬜ Not started | Door interaction | Lua | `scripts/door.lua` | Project | Requires keys before leaving |
| ⬜ Not started | Dark-figure encounter | Lua | `scripts/dark_figure.lua` | Project | Whisper, approach response, disappearance |
| ⬜ Not started | Opening mother and child dialogue | Voice audio | `audio/dialogue/opening/` |  | See dialogue manifest below |
| ⬜ Not started | Apartment ambient loop | Audio | `audio/ambience/apartment_room_tone.wav` |  | Quiet neutral room bed |
| ⬜ Not started | Refrigerator hum | Audio | `audio/ambience/refrigerator_hum.wav` |  | Mono positional source |
| ⬜ Not started | Traffic through window | Audio | `audio/ambience/morning_traffic.wav` |  | Distant and subdued |
| ⬜ Not started | Neighbor or television through wall | Audio | `audio/ambience/neighbor_tv.wav` |  | Helps establish normality |
| ⬜ Not started | Heartbeat layers | Audio | `audio/player/heartbeat/` |  | Calm, tense, and intense variants |
| ⬜ Not started | Breathing layers | Audio | `audio/player/breathing/` |  | Calm, tense, and recovery variants |
| ⬜ Not started | “Look at you” whisper | Voice audio | `audio/dialogue/dark_figures/look_at_you.wav` |  | First figure line |
| ⬜ Not started | Footstep set | Audio | `audio/player/footsteps/` |  | Minimum wood, carpet, and concrete |
| ⬜ Not started | Key jingle | Audio | `audio/sfx/keys_jingle.wav` |  | Interaction feedback |
| ⬜ Not started | Door sounds | Audio | `audio/sfx/doors/` |  | Handle, locked, open, close |
| ⬜ Not started | Apartment morning HDR | HDR | `hdr/apartment_morning.hdr` |  | Optional if interior lighting works without a visible sky |

## Environment and location assets

### Apartment and apartment building

| Status | Priority | Asset | Suggested path | Source | Notes |
|---|---|---|---|---|---|
| ⬜ Not started | P0 | Modular walls, floors, and ceilings | `models/apartment/modular/` |  | Reusable interior pieces |
| ⬜ Not started | P0 | Doorframes and interior doors | `models/apartment/doors/` |  | Separate movable door leaves |
| ⬜ Not started | P0 | Windows and curtains | `models/apartment/windows/` |  | Curtains can hide exterior limitations |
| ⬜ Not started | P0 | Kitchen cabinets, counter, and sink | `models/apartment/kitchen/` |  | Grounded ordinary apartment |
| ⬜ Not started | P0 | Bathroom fixtures | `models/apartment/bathroom/` |  | Toilet, sink, shower or bath |
| ⬜ Not started | P0 | Light switches and outlets | `models/apartment/fixtures/` |  | Must match the chosen country |
| ⬜ Not started | P0 | Lamps and ceiling fixtures | `models/apartment/lighting/` |  | Light meshes plus scene spot lights |
| ⬜ Not started | P1 | Building corridor and landing | `models/building/corridor.gltf` |  | First possible dark-figure location |
| ⬜ Not started | P1 | Stairwell or elevator lobby | `models/building/stairwell.gltf` |  | Choose one based on the building reference |
| ⬜ Not started | P1 | Mailboxes, notices, and number signs | `models/building/props/` |  | Regional detail |

### Streets and town

| Status | Priority | Asset | Suggested path | Source | Notes |
|---|---|---|---|---|---|
| ⬜ Not started | P1 | Route from apartment to work | `models/town/work_route.gltf` |  | Small controlled route, not an open world |
| ⬜ Not started | P1 | Road, pavement, and curb kit | `models/town/streets/` |  | Modular pieces |
| ⬜ Not started | P1 | Residential and shop facades | `models/town/buildings/` |  | Background shells can be low detail |
| ⬜ Not started | P1 | Streetlights | `models/town/streetlights.gltf` |  | Important recurring visual and audio source |
| ⬜ Not started | P1 | Parked and moving car set | `models/vehicles/cars/` |  | Include the final-impact vehicle later |
| ⬜ Not started | P1 | Road signs and markings | `models/town/signs/` |  | Match chosen location |
| ⬜ Not started | P1 | Benches, bins, fences, and utility boxes | `models/town/street_props/` |  | Reusable dressing set |
| ⬜ Not started | P1 | Trees and small vegetation | `models/town/vegetation/` |  | Keep scope modest |
| ⬜ Not started | P2 | Final road at night | `models/town/final_road.gltf` |  | Must support the final figure and headlights |
| ⬜ Not started | P2 | Night street HDR | `hdr/night_street.hdr` |  | Final sequence and late-game exterior scenes |

### Workplace

| Status | Priority | Asset | Suggested path | Source | Notes |
|---|---|---|---|---|---|
| ⬜ Not started | P1 | Workplace building shell | `models/workplace/workplace.gltf` |  | Cashier or customer-facing workplace |
| ⬜ Not started | P1 | Checkout counter and register | `models/workplace/checkout/` |  | Main confrontation location |
| ⬜ Not started | P1 | Shelves and product set | `models/workplace/products/` |  | Use grouped low-detail product variants |
| ⬜ Not started | P1 | Employee entrance and back room | `models/workplace/back_room/` |  | Includes clock-in point |
| ⬜ Not started | P1 | Clock-in terminal | `models/workplace/clock_terminal.gltf` |  | Mundane interaction objective |
| ⬜ Not started | P1 | Workplace signage and price labels | `textures/workplace/signage/` | Project | Match chosen town and language |
| ⬜ Not started | P1 | Cleaning and storage props | `models/workplace/storage/` |  | Back-room dressing |

### Party memory

| Status | Priority | Asset | Suggested path | Source | Notes |
|---|---|---|---|---|---|
| ⬜ Not started | P2 | Party house shell | `models/party_house/party_house.gltf` |  | Living room, kitchen, hall, stairs, bathroom |
| ⬜ Not started | P2 | Party furniture set | `models/party_house/furniture/` |  | Couch, tables, chairs, cabinets |
| ⬜ Not started | P2 | Cups, bottles, snacks, and clutter | `models/party_house/props/` |  | Dense but believable dressing |
| ⬜ Not started | P2 | Speaker and music setup | `models/party_house/speakers.gltf` |  | Positional source for muffled music behavior |
| ⬜ Not started | P2 | Phone charger | `models/props/phone_charger.gltf` |  | Possible exploration objective |
| ⬜ Not started | P2 | Jackets and bags | `models/party_house/jackets/` |  | Possible exploration objective |
| ⬜ Not started | P2 | Drug story prop | `models/story/party_drug_prop.gltf` |  | Keep visually grounded, not sensationalized |
| ⬜ Not started | P2 | White figure set | `models/figures/white_figures/` |  | Static poses are enough for the first memory pass |

### Childhood and unreliable-memory spaces

| Status | Priority | Asset | Suggested path | Source | Notes |
|---|---|---|---|---|---|
| ⬜ Not started | P2 | Childhood bedroom | `models/memories/childhood_bedroom.gltf` |  | Supports reconstruction puzzle |
| ⬜ Not started | P2 | Childhood furniture and toy set | `models/memories/childhood_props/` |  | Objects must be individually movable |
| ⬜ Not started | P2 | School books and supplies | `models/memories/school_items/` |  | Reinforces early ambition |
| ⬜ Not started | P2 | Toy doctor kit or stethoscope | `models/memories/doctor_kit.gltf` |  | Strong visual motif |
| ⬜ Not started | P2 | Incorrect and corrected prop layouts | Scene content | `scenes/memory_childhood.json` | Project | Same space shown with altered truth |
| ⬜ Not started | P2 | Overexposed memory material set | Textures | `textures/memories/` | Project | White, washed-out, restrained look |

### Optional locations pending story decisions

| Status | Priority | Asset | Suggested path | Decision needed |
|---|---|---|---|---|
| ⬜ Not started | Optional | Convenience store | `models/optional/convenience_store/` | Confirm whether separate from workplace |
| ⬜ Not started | Optional | Bus stop and parking area | `models/optional/transit/` | Confirm travel sequences |
| ⬜ Not started | Optional | Friend’s home | `models/optional/friend_home/` | Confirm Act II structure |
| ⬜ Not started | Optional | Hospital or clinic | `models/optional/clinic/` | Confirm whether shown or only referenced |
| ⬜ Not started | Optional | School | `models/optional/school/` | Confirm memory locations |
| ⬜ Not started | Optional | Protagonist’s childhood home exterior | `models/optional/childhood_home/` | Confirm memory scope |

## General prop library

| Status | Priority | Asset set | Suggested path | Notes |
|---|---|---|---|---|
| ⬜ Not started | P0 | Bedroom furniture | `models/furniture/bedroom/` | Bed, nightstand, wardrobe, lamp |
| ⬜ Not started | P0 | Living-room furniture | `models/furniture/living_room/` | Couch, chair, table, television stand |
| ⬜ Not started | P0 | Kitchen objects | `models/props/kitchen/` | Dishes, cups, food packaging, kettle |
| ⬜ Not started | P0 | Bathroom objects | `models/props/bathroom/` | Towels, toiletries, medication |
| ⬜ Not started | P0 | Personal clutter | `models/props/personal/` | Wallet, phone, charger, work badge, shoes |
| ⬜ Not started | P0 | Cleaning and rubbish | `models/props/cleaning/` | Bin, bags, broom, containers |
| ⬜ Not started | P1 | Paper and mail props | `models/props/paper/` | Letters, bills, notices, receipts |
| ⬜ Not started | P1 | Town dressing set | `models/town/street_props/` | Reuse across exterior scenes |
| ⬜ Not started | P2 | Hospital aftermath props | `models/story/medical/` | Only needed if anything is visually shown |

## Characters and figures

| Status | Priority | Character or variant | Suggested path | Animation need | Notes |
|---|---|---|---|---|---|
| ⬜ Not started | P0 | Dark figure, distant standing pose | `models/figures/dark_figure_standing.gltf` | None | First encounter |
| ⬜ Not started | P1 | Dark figure pose variants | `models/figures/dark_figures/` | None initially | Hall, street, workplace, reflection poses |
| ⬜ Not started | P2 | White figure pose variants | `models/figures/white_figures/` | None initially | Party memory and regret sequences |
| ⏸ Blocked | P1 | Boss | `models/characters/boss/` | Talking and gesture animation | Needs skeletal animation system |
| ⏸ Blocked | P1 | Pedestrian variants | `models/characters/pedestrians/` | Idle and walking | Needs skeletal animation system |
| ⏸ Blocked | P1 | Coworkers and customers | `models/characters/workplace/` | Idle, talk, simple actions | Needs skeletal animation system |
| ⏸ Blocked | P2 | Younger protagonist | `models/characters/younger_protagonist/` | Party actions | Needs skeletal animation system |
| ⏸ Blocked | P2 | Friends and party guests | `models/characters/party_guests/` | Idle, talk, party loops | Needs skeletal animation system |
| ⏸ Blocked | P2 | Child protagonist | `models/characters/child_protagonist/` | Look and talk | Only needed if visually shown |
| ⏸ Blocked | P2 | Mother | `models/characters/mother/` | Talk and gesture | Audio-only in the opening |
| ⬜ Not started | Optional | First-person arms and hands | `models/characters/player_hands/` | Interaction animations | Not required for the first slice |

## Animation manifest

These are asset requirements only if the final presentation shows animated people.

| Status | Priority | Animation set | Character group | Notes |
|---|---|---|---|---|
| ⏸ Blocked | P1 | Idle and breathing | All visible characters | Needs skeletal animation support |
| ⏸ Blocked | P1 | Walk cycles | Pedestrians and coworkers | Include multiple speeds |
| ⏸ Blocked | P1 | Conversation gestures | Boss, coworkers, friends | Small grounded gestures |
| ⏸ Blocked | P1 | Cashier and workplace actions | Workplace characters | Register, stocking, waiting |
| ⏸ Blocked | P2 | Party idle loops | Party guests | Talking, drinking, sitting |
| ⏸ Blocked | P2 | Drug acceptance sequence | Younger protagonist | Could instead remain first-person and audio-driven |
| ⏸ Blocked | P2 | Final vehicle movement | Impact vehicle | Can be scripted transform motion, no skeleton needed |
| ⏸ Blocked | Optional | First-person interactions | Player hands | Keys, doors, phone, objects |

## Audio asset manifest

### Player audio

| Status | Priority | Audio set | Suggested path | Notes |
|---|---|---|---|---|
| ⬜ Not started | P0 | Calm breathing | `audio/player/breathing/calm.wav` | Quiet baseline |
| ⬜ Not started | P0 | Tense breathing | `audio/player/breathing/tense.wav` | Figure proximity |
| ⬜ Not started | P0 | Recovery breathing | `audio/player/breathing/recovery.wav` | Transition back to normal |
| ⬜ Not started | P0 | Heartbeat intensity layers | `audio/player/heartbeat/` | Calm, tense, intense |
| ⬜ Not started | P0 | Wood footsteps | `audio/player/footsteps/wood/` | At least four variations |
| ⬜ Not started | P0 | Carpet footsteps | `audio/player/footsteps/carpet/` | At least four variations |
| ⬜ Not started | P1 | Concrete footsteps | `audio/player/footsteps/concrete/` | Hall and street |
| ⬜ Not started | P1 | Tile footsteps | `audio/player/footsteps/tile/` | Workplace and bathroom |
| ⬜ Not started | P1 | Clothing movement | `audio/player/clothing/` | Optional low-volume movement layer |

### Environmental ambience

| Status | Priority | Audio | Suggested path | Notes |
|---|---|---|---|---|
| ⬜ Not started | P0 | Apartment room tone | `audio/ambience/apartment_room_tone.wav` | Looping |
| ⬜ Not started | P0 | Refrigerator hum | `audio/ambience/refrigerator_hum.wav` | Positional |
| ⬜ Not started | P0 | Distant morning traffic | `audio/ambience/morning_traffic.wav` | Window source |
| ⬜ Not started | P0 | Neighbor television | `audio/ambience/neighbor_tv.wav` | Through-wall sound |
| ⬜ Not started | P0 | Pipes and building creaks | `audio/ambience/building/` | Sparse one-shots |
| ⬜ Not started | P1 | Apartment corridor tone | `audio/ambience/apartment_corridor.wav` | Looping |
| ⬜ Not started | P1 | Street daytime ambience | `audio/ambience/street_day.wav` | Traffic, wind, distant people |
| ⬜ Not started | P1 | Streetlight electrical hum | `audio/ambience/streetlight_hum.wav` | Positional |
| ⬜ Not started | P1 | Workplace ambience | `audio/ambience/workplace.wav` | Refrigeration, customers, room tone |
| ⬜ Not started | P2 | Party crowd loop | `audio/ambience/party_crowd.wav` | Must blend with music |
| ⬜ Not started | P2 | Party music and muffled variant | `audio/music/party/` | Record licensing and create through-wall version |
| ⬜ Not started | P2 | Night road ambience | `audio/ambience/night_road.wav` | Final sequence |

### Interaction and object sounds

| Status | Priority | Audio set | Suggested path | Notes |
|---|---|---|---|---|
| ⬜ Not started | P0 | Phone alarm | `audio/sfx/phone_alarm.wav` | Opening wake-up |
| ⬜ Not started | P0 | Phone vibration | `audio/sfx/phone_vibration.wav` | Recurring exposition tool |
| ⬜ Not started | P0 | Keys pickup and jingle | `audio/sfx/keys/` | Search puzzle |
| ⬜ Not started | P0 | Door handle, locked, open, close | `audio/sfx/doors/` | Multiple door types later |
| ⬜ Not started | P0 | Drawer and cabinet sounds | `audio/sfx/furniture/` | Searching interactions |
| ⬜ Not started | P0 | Light switch | `audio/sfx/light_switch.wav` | Apartment interactions |
| ⬜ Not started | P1 | Clock-in terminal | `audio/sfx/clock_in.wav` | Workplace objective |
| ⬜ Not started | P1 | Register and scanner | `audio/sfx/register/` | Workplace texture |
| ⬜ Not started | P1 | Vehicle pass-bys and horns | `audio/sfx/vehicles/` | Street encounters |
| ⬜ Not started | P2 | Message notification variants | `audio/sfx/phone_notifications/` | Phone system |

### Horror and perception sounds

| Status | Priority | Audio | Suggested path | Notes |
|---|---|---|---|---|
| ⬜ Not started | P0 | Dark-figure low tension bed | `audio/horror/figure_tension.wav` | Subtle loop, avoid constant stings |
| ⬜ Not started | P0 | Figure disappearance transition | `audio/horror/figure_disappear.wav` | May be near-silent |
| ⬜ Not started | P1 | Muffled-world transition | `audio/horror/perception_muffle.wav` | Supports environmental sound reduction |
| ⬜ Not started | P1 | Tinnitus or focus tone | `audio/horror/focus_tone.wav` | Use sparingly |
| ⬜ Not started | P1 | Rare sharp sting | `audio/horror/sharp_sting.wav` | Reserved for the party’s first figure |
| ⬜ Not started | P2 | Memory transition sounds | `audio/horror/memory_transitions/` | Present-to-memory blending |
| ⬜ Not started | P2 | Final horn and impact | `audio/horror/final_impact/` | Immediate darkness after impact |
| ⬜ Not started | P2 | Emergency aftermath | `audio/horror/emergency_aftermath.wav` | Crowd, footsteps, distant sirens |
| ⬜ Not started | P2 | Approaching and fading sirens | `audio/horror/sirens/` | Final sound-led scene |

### Dialogue and voice recording packages

| Status | Priority | Dialogue package | Suggested path | Required voices | Notes |
|---|---|---|---|---|---|
| ⬜ Not started | P0 | Opening doctor conversation | `audio/dialogue/opening/` | Mother, child | Record each line separately plus complete timing reference |
| ⬜ Not started | P0 | First “Look at you” whisper | `audio/dialogue/dark_figures/look_at_you.wav` | Dark figure | Dry recording retained for later processing |
| ⬜ Not started | P0 | Key-search mutters | `audio/dialogue/protagonist/keys/` | Protagonist | Optional restrained reactions |
| ⬜ Not started | P1 | Boss confrontation | `audio/dialogue/workplace/boss_confrontation/` | Boss, protagonist | Includes interruptions from figure whispers |
| ⬜ Not started | P1 | Dark-figure accusation set | `audio/dialogue/dark_figures/accusations/` | Figure voices | “Late again,” “Failure,” “He hates you,” and related lines |
| ⬜ Not started | P1 | Dark-figure excuse set | `audio/dialogue/dark_figures/excuses/` | Figure voices | “It wasn’t your fault,” “Just one drink,” and related lines |
| ⬜ Not started | P2 | Party invitation messages | `audio/dialogue/party/invitation/` | Friends, protagonist | If presented as calls or voiced memories |
| ⬜ Not started | P2 | Party conversation | `audio/dialogue/party/conversation/` | Friends, younger protagonist | Includes the offer and consent variations |
| ⬜ Not started | P2 | White-figure warning set | `audio/dialogue/white_figures/` | Older protagonist-like voice | Includes “You still have time” and “I’m sorry” |
| ⬜ Not started | P2 | Childhood memory variations | `audio/dialogue/memories/childhood/` | Mother, child | Warm, fragmented, and confrontational versions |
| ⬜ Not started | P2 | Final figure confrontation | `audio/dialogue/final_figure/` | Protagonist, figure | “What do you want from me?” sequence |
| ⬜ Not started | P2 | Emergency responder dialogue | `audio/dialogue/final_aftermath/` | Bystanders, paramedic | Mostly muffled |
| ⬜ Not started | P2 | Final question | `audio/dialogue/ending/who_is_your_lord.wav` | Final voice | Completely clear and untreated compared with hallucinations |

## Textures, materials, and decals

| Status | Priority | Asset set | Suggested path | Notes |
|---|---|---|---|---|
| ⬜ Not started | P0 | Painted apartment walls | `textures/surfaces/painted_wall/` | Several subtle wear variants |
| ⬜ Not started | P0 | Wood or laminate floor | `textures/surfaces/wood_floor/` | Apartment main surface |
| ⬜ Not started | P0 | Carpet | `textures/surfaces/carpet/` | Bedroom or hallway |
| ⬜ Not started | P0 | Kitchen and bathroom tile | `textures/surfaces/tile/` | Floor and wall variants |
| ⬜ Not started | P0 | Door and cabinet wood | `textures/surfaces/woodwork/` | Match apartment age |
| ⬜ Not started | P0 | Glass and window dirt | `textures/surfaces/glass/` | Restrained grime |
| ⬜ Not started | P0 | Childhood drawing | `textures/story/doctor_drawing.png` | Original authored graphic |
| ⬜ Not started | P0 | School certificate | `textures/story/school_certificate.png` | Original authored graphic |
| ⬜ Not started | P0 | Bills and overdue notices | `textures/story/bills/` | Fictional names and account details |
| ⬜ Not started | P1 | Workplace signs and labels | `textures/workplace/signage/` | Fictional brands only |
| ⬜ Not started | P1 | Street signs and road markings | `textures/town/signage/` | Match setting |
| 🟡 In development | P1 | Asphalt surface | `textures/asphalt/` | Albedo, OpenGL normal, and packed ARM maps placed in `scenes/horror.json`; source and license still need recording |
| ⬜ Not started | P1 | Dirt, leaks, scratches, and wall marks | `textures/decals/environment/` | Use decals or plane overlays |
| ⬜ Not started | P2 | Dark-figure material set | `textures/figures/dark/` | Must retain a readable silhouette without obvious detail |
| ⬜ Not started | P2 | White-figure material set | `textures/figures/white/` | Work around the lack of emissive textures |
| ⬜ Not started | P2 | Memory-space variants | `textures/memories/` | Warmer early memory, colder corrected memory |

## UI and 2D assets

| Status | Priority | Asset | Suggested path | Notes |
|---|---|---|---|---|
| ⬜ Not started | P0 | Interaction prompt treatment | `textures/ui/interaction/` | Text-only is acceptable for the prototype |
| ⬜ Not started | P0 | Subtitle style reference | `references/ui/subtitles/` | Choose size, color, placement, speaker rules |
| ⬜ Not started | P1 | Main title or wordmark | `textures/ui/title.png` | Working title can remain text initially |
| ⬜ Not started | P1 | Main menu background | `textures/ui/main_menu.png` | May be rendered in-engine instead |
| ⬜ Not started | P1 | Pause and settings icons | `textures/ui/settings/` | Audio, controls, display |
| ⬜ Not started | P2 | Phone frame and screen background | `textures/ui/phone/phone_frame.png` | Phone system requires engine/UI work |
| ⬜ Not started | P2 | Phone app icons | `textures/ui/phone/icons/` | Messages, calls, contacts |
| ⬜ Not started | P2 | Contact avatars | `textures/ui/phone/contacts/` | Fictional people |
| ⬜ Not started | P2 | Message attachment images | `textures/ui/phone/attachments/` | Only if required by story |
| ⬜ Not started | P2 | Credits layout and logos | `textures/ui/credits/` | Include third-party attribution where required |

## Scene files

| Status | Priority | Scene | Suggested path | Notes |
|---|---|---|---|---|
| 🟡 In development | P0 | Opening apartment | `scenes/horror.json` | Currently an asphalt material preview; apartment layout not started |
| ⬜ Not started | P1 | Apartment corridor or exit | `scenes/apartment_building.json` | Can remain part of apartment scene if small |
| ⬜ Not started | P1 | Walk to work | `scenes/work_route.json` | Includes first street figure |
| ⬜ Not started | P1 | Workplace | `scenes/workplace.json` | Boss confrontation |
| ⬜ Not started | P2 | First drug memory and party | `scenes/party_memory.json` | White figures and first dark figure |
| ⬜ Not started | P2 | Childhood memory | `scenes/childhood_memory.json` | Doctor conversation and reconstruction puzzle |
| ⬜ Not started | P2 | Revisited party memory | `scenes/party_memory_corrected.json` | Reveals altered dialogue and responsibility |
| ⬜ Not started | P2 | Final night road | `scenes/final_road.json` | Figure, vehicle, blackout, aftermath |
| ⬜ Not started | P2 | Credits | `scenes/credits.json` or UI sequence | Can remain in the final-road scene |

## Lua script assets

| Status | Priority | Script | Suggested path | Responsibility |
|---|---|---|---|---|
| ⬜ Not started | P0 | First-person controller | `scripts/fps_controller.lua` | Movement, look, and footsteps |
| ⬜ Not started | P0 | Opening sequence director | `scripts/opening_sequence.lua` | Conversation timing and apartment state |
| ⬜ Not started | P0 | Keys interaction | `scripts/keys.lua` | Search progression and appearance |
| ⬜ Not started | P0 | Door interaction | `scripts/door.lua` | Locked and unlocked behavior |
| ⬜ Not started | P0 | Dark figure behavior | `scripts/dark_figure.lua` | Distance response, sound, shake, disappearance |
| ⬜ Not started | P0 | Subtitle queue | `scripts/subtitles.lua` | Timed lines and fades |
| ⬜ Not started | P0 | Horror post-process controller | `scripts/perception_effects.lua` | Vignette, aberration, grain, and recovery |
| ⬜ Not started | P1 | Narrative trigger | `scripts/narrative_trigger.lua` | Reusable one-shot area event after trigger support exists |
| ⬜ Not started | P1 | Light flicker | `scripts/light_flicker.lua` | Controlled spot-light intensity changes |
| ⬜ Not started | P1 | Workplace sequence | `scripts/workplace_sequence.lua` | Clock-in and boss conversation |
| ⬜ Not started | P1 | Street encounter director | `scripts/street_encounter.lua` | Figure and passing vehicle timing |
| ⬜ Not started | P2 | Phone and messages | `scripts/phone.lua` | Message history and unreliable changes |
| ⬜ Not started | P2 | Memory transition | `scripts/memory_transition.lua` | Fade, audio, and scene switching |
| ⬜ Not started | P2 | Memory reconstruction puzzle | `scripts/memory_puzzle.lua` | Object placement and corrected memory reveal |
| ⬜ Not started | P2 | Party sequence | `scripts/party_sequence.lua` | Music, dialogue, white figures, first dark figure |
| ⬜ Not started | P2 | Final road sequence | `scripts/final_road.lua` | Figure rule break, headlights, impact, and blackout |
| ⬜ Not started | P2 | Ending and credits | `scripts/ending.lua` | Emergency audio, final question, credits |

## Engine dependencies and blockers

These are not assets, but the corresponding content cannot be completed cleanly without them.

| Status | Priority | Dependency | Needed by | Notes |
|---|---|---|---|---|
| ⬜ Not started | P0 | Character enter/exit trigger callbacks | Apartment and every narrative sequence | CharacterVirtual does not currently dispatch sensor events to Lua |
| ⬜ Not started | P0 | Scriptable entity visibility | Keys and figures | Avoid destroying entities that must reappear later |
| ⬜ Not started | P1 | Runtime scene transitions | Multi-location story | Load the next scene safely during Play mode |
| ⬜ Not started | P1 | Footstep surface selection | Player audio | Select samples by floor material or tagged area |
| ⏸ Blocked | P1 | Skeletal animation and skinned meshes | Visible human characters | Not required for static P0 figures |
| ⬜ Not started | P2 | Dialogue sequencing system | Boss, party, memories | Lua-only prototype is possible first |
| ⬜ Not started | P2 | Phone UI and input capture | Text-message exposition | Current immediate UI is enough only for simple prototypes |
| ⬜ Not started | P2 | Checkpoint or narrative save state | Full story | Not required for the opening slice |
| ⬜ Not started | P2 | OGG decoding and audio streaming | Long ambience and music | WAV is sufficient for the first slice |
| ⬜ Not started | Optional | Reverb zones | Apartment, corridors, party | OpenAL EFX work |
| ⬜ Not started | Optional | Audio occlusion | Voices and sources through walls | Physics raycast integration |
| ⬜ Not started | Optional | Point lights and Forward+ | Dense town and party lighting | Wide spot lights can cover P0 |
| ⬜ Not started | Optional | True mirror rendering | Unreliable reflections | Fake reflection scenes or textures are acceptable |

## Source and licensing register

Add one row for every downloaded model, texture pack, sound library, music track, font, or voice
recording. Keep the original license file beside the asset when redistribution requires it.

| Status | Asset or pack | Author | Source URL or local origin | License | License path | Modifications |
|---|---|---|---|---|---|---|
| ⬜ Not started |  |  |  |  |  |  |

## Milestone readiness checks

### P0 opening apartment vertical slice

- [ ] Apartment is navigable at real-world scale.
- [ ] Player collision and first-person controls work.
- [ ] Opening mother and child conversation plays correctly.
- [ ] Environmental loops are balanced and spatialized.
- [ ] The player can search for and obtain the keys.
- [ ] The front door reacts correctly before and after finding the keys.
- [ ] The dark figure appears, whispers, affects perception, and disappears on approach.
- [ ] Returning to normal restores audio and post-process settings.
- [ ] The sequence reaches a deliberate ending or fade.
- [ ] The scene exports and runs from an extracted package outside the repository.

### P1 Act I

- [ ] Apartment building exit is complete.
- [ ] Walk-to-work route is complete.
- [ ] Pedestrians and vehicles sell an ordinary town.
- [ ] First street figure encounter works.
- [ ] Workplace objective and boss confrontation work.
- [ ] Real dialogue and figure whispers can overlap intelligibly.
- [ ] Scene transitions preserve the intended narrative state.

### P2 full story treatment

- [ ] Party memory and white figures are complete.
- [ ] First dark-figure origin encounter is complete.
- [ ] Unreliable-memory revisit is complete.
- [ ] Childhood doctor motif recurs with escalating variations.
- [ ] Phone history supports relationship exposition.
- [ ] Late-game figure escalation remains readable without becoming conventional combat.
- [ ] Final road rule-break and impact sequence are complete.
- [ ] Emergency aftermath is understandable through sound alone.
- [ ] Final question and credits play correctly.
- [ ] All licenses and credits are complete.
