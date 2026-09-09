# Apartment glazing

`glass_pane.gltf` is an original, reusable one-meter-square plane in the XY plane,
centered at the origin and facing +Z. It contains two triangles and an embedded buffer.
The scene scales six instances into the balcony door and living-room window openings.

The material uses core glTF alpha blending (`BLEND`), double-sided rendering, 10% opacity,
metallic factor 0, and roughness 0.12. No optional or required glTF extensions are needed.
It is lightweight tinted transparency, not refraction or a transmission shader.

Frames and collision blockers remain editable scene entities in `scenes/apartment.json`.
The closed door and window are currently scenery and have no opening interaction.
