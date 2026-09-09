# Apartment scene polish

The opening dialogue runs for 21 seconds, followed by the 3.2-second wake-up transition.
Movement, mouse look, and interactions remain locked until the camera handoff finishes.
Subtitles are drawn over an ordinary black UI rectangle so the fade overlay cannot cover them.

The key search now follows these prompts:

1. Check the kitchen counter.
2. Check the coat stand by the front door.
3. Look between the sofa cushions.
4. Check the kitchen counter again.
5. Pick up the keys and leave the apartment.

Only the currently requested search advances the route. Other props still respond. The keys
appear after the sofa response has finished, in the wooden tray beside the fruit bowl.
The key root is at `(5.30, 1.508, 8.30)`, with its lowest geometry resting on the tray.
The routes and responses are in `games/horror/assets/scripts/opening_sequence.lua`.

## Visual changes

- The renderer now applies glTF metallic and roughness factors and normal-map strength.
  The shell's roughness images have white blue channels, so ignoring `metallicFactor = 0`
  incorrectly made plaster, ceiling, and timber fully metallic.
- The shell uses consistent projected UV scales: a 2.4-meter repeat for plaster and ceiling,
  and a 2.8-meter repeat for the wood floor. The timber texture contains multiple planks per repeat.
- Plaster and ceiling normals are reduced to 0.30 and 0.18. The floor uses 0.50.
  Material tints slightly cool the plaster and reduce the timber's orange cast.
- `Apartment_Dressing` groups the additions: skirting along the wall boundaries, a key tray,
  two letters, a shelf with books, a low coffee table, and a living-room rug.
  The table has a static box collider. The rug reuses the existing bathroom rug model,
  whose material roughness is now 0.94 instead of 0.055.
- The trim and furniture are ordinary scene boxes, editable in the engine.

## Living-room glazing and courtyard

- The three shelf books now use navy blue, sage green, and oxblood cloth textures.
  These were generated with the built-in image tool, using the imagegen skill. The files
  and full generation prompts are in `games/horror/assets/textures/books/README.md`.
  Thin decorative bands distinguish the spines from blocks of wood.
- `Livingroom_BalconyDoor` fills the existing opening with a closed double door, handles,
  lower panels, a threshold, and two clear panes. The wall above it reaches the ceiling.
- `Livingroom_Window` fits the existing window opening and its high horizontal beam.
  Four separate panes, painted frames, a center mullion, and latches complete it.
  The existing shelf remains below the window, clear of the framing.
- `FrontDoor_HeaderWall` and `FrontDoor_HeaderTrim` close the gap above the entrance frame.
  The front door and its existing script are unchanged.
- `Livingroom_Balcony` contains the door/window groups, concrete floor, upper slab, and railing.
  The door and window have static blocking colliders. They do not open or provide a route
  outside in this prototype. The balcony is scenery, not an additional playable area.
- `Apartment_OutdoorView` contains a three-sided brick courtyard built from the existing
  modular kit, plus paving, a bench, and two existing street-lamp models. Its single cool
  facade fill does not cast shadows or consume a spotlight shadow-map slot. The lamps
  are visual dressing without added light sources.

Frames, headers, paving, railings, and bench are scene boxes. The only new mesh asset is
`models/apartment/windows/glass_pane.gltf`: a reusable two-triangle, double-sided pane with
10% alpha, zero metallic factor, and no transmission extension. Its buffer is embedded.
Each pane is a separate entity to avoid overlapping front/back transparent surfaces.
This uses existing alpha blending, not refraction or physically accurate glass transmission.
The original apartment shell and Blender file were not changed for these additions.

These changes update the exported shell glTF and scene JSON. The original Blender source and
mesh binary are unchanged. Re-exporting the Blender shell over this glTF will replace the UV
and material adjustments; port them to the source file when continuing work in Blender.
The added UV data is embedded in the glTF, so it needs no additional external file when exported.

## Verification

The engine library and both forward shaders compile successfully. The standalone Lua check
in `tools/check_apartment_story.lua` exercises input lock, readable subtitle timing, ordered
and repeated searches, delayed reveal, pickup, the door, and a fresh Play session.

`node tools/check_apartment_dressing.cjs` checks referenced assets, new parent chains,
book textures, static blockers, ceiling infills, and the embedded glass buffer. The original
scene entries are preserved except for the nine book cover/spine texture references.
`tools/preview_apartment.py` can render the scene in Blender background mode without running
the engine. Its object naming isolates model imports so saved node overrides resolve correctly.

Blender previews were used to check placement and materials. They use separate preview lighting
and do not verify the Vulkan renderer or in-game interaction targeting. Restart the engine after
rebuilding to reload the cached model and inspect the actual lighting and route in Play mode.
