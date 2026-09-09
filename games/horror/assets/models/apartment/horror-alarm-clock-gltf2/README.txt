NIGHTFALL — HORROR GAME ALARM CLOCK

Original aged olive-green twin-bell clock designed for the supplied room.
Import alarm_clock.glb for a single-file asset. This is binary glTF 2.0.
Alternatively import alarm_clock.gltf; keep alarm_clock.bin and textures/ beside it.

MODEL
5,908 triangles. 5 named mesh parts. Three PBR materials. Static details merged; five render primitives total.
One 1024 x 1024 base-color atlas and one 1024 x 1024 ORM atlas.
ORM: R = white (no baked AO), G = roughness, B = metallic.
Base color is sRGB; ORM is linear. UV0 supplied; no normal map is required.
Smooth geometry normals, no external extensions or compression required.
Units: metres. Y up. Clock front faces +Z. Root at floor contact, Y = 0.
Approximately 21 cm tall. Default face time 03:07:42.
Recessed face without a transparent glass mesh for reliable game import.

ANIMATION
Hour_Hand, Minute_Hand and Second_Hand are separate nodes pivoted at the dial spindle.
Rotate each around its local Z axis; clockwise viewed from the front is negative Z.
The shared hand origin is (0, 0.092, 0), with face depth in the mesh vertices.
Alarm_Ring animates Hammer_Pivot over 0.6 seconds; enable looping in your engine.
No audio, ticking behavior, scripts, collision shapes or LODs are embedded.
Use a simple engine collision shape around the body for placement/interactions.
Set the time and alarm behavior in game code.

DESIGN
Original procedural geometry, scratches, tarnish and printed dial artwork.
NIGHTFALL is illustrative fictional dial branding.
Suitable as a small environmental prop on the wooden TV table or a bedside surface.
The preview uses neutral studio lighting; appearance will follow your engine lighting.
