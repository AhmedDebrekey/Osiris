import json
import math
from pathlib import Path

import bpy
from mathutils import Euler, Matrix, Vector


root = Path(__file__).resolve().parents[1]
assets = root / 'games/horror/assets'
entries = json.loads((assets / 'scenes/apartment.json').read_text())['entities']
by_name = {e['name']: e for e in entries}
conversion = Matrix.Rotation(math.pi / 2, 4, 'X')
inverse = conversion.inverted()


def transform(data):
    return Matrix.LocRotScale(Vector(data.get('position', [0, 0, 0])),
                             Euler([math.radians(v) for v in data.get('rotation', [0, 0, 0])], 'ZYX').to_quaternion(),
                             Vector(data.get('scale', [1, 1, 1])))


def world(entry):
    local = transform(entry.get('transform', {}))
    return world(by_name[entry['parent']]) @ local if entry.get('parent') in by_name else local


def import_model(entry):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(assets / entry['mesh']))
    objects = set(bpy.data.objects) - before
    placement = bpy.data.objects.new(entry['name'] + '_placement', None)
    bpy.context.collection.objects.link(placement)
    overrides = [e for e in entries if e.get('modelChild') == entry['mesh']]
    for obj in objects:
        if obj.parent not in objects:
            obj.parent = placement
        for saved in overrides:
            if saved['name'].rsplit('_', 1)[0] == entry['name'] + '_' + obj.name:
                obj.matrix_basis = conversion @ transform(saved.get('transform', {})) @ inverse
    placement.matrix_world = conversion @ world(entry) @ inverse
    bpy.context.view_layer.update()
    # Isolate object names so Blender's .001 suffixes cannot misroute scene overrides.
    for obj in objects:
        obj.name = entry['name'] + '_' + obj.name
    return objects


def bounds(objects):
    corners = [inverse @ o.matrix_world @ Vector(v) for o in objects if o.type == 'MESH' for v in o.bound_box]
    return [[round(fn(v[i] for v in corners), 4) for i in range(3)] for fn in (min, max)]


def inspect():
    for name in ['apartment_shell', 'front_door']:
        objects = import_model(by_name[name])
        print('BOUNDS', name, bounds(objects), flush=True)
        if name == 'apartment_shell':
            for obj in objects:
                if obj.type != 'MESH' or 'Walls' not in obj.name:
                    continue
                verts = [inverse @ obj.matrix_world @ v.co for v in obj.data.vertices]
                print('NORTH', obj.name, sorted(set(tuple(round(c, 4) for c in v) for v in verts if v.z < -5.3)), flush=True)
                print('ENTRY', obj.name, sorted(set(tuple(round(c, 4) for c in v) for v in verts if v.x > 6 and 2 < v.z < 5)), flush=True)
    objects = import_model({'name': 'FacadeSample', 'mesh': 'models/modular_exterior_parts/window_002_0.gltf'})
    print('BOUNDS FacadeSample', bounds(objects), flush=True)


def preview():
    for entry in entries:
        if entry.get('mesh'):
            import_model(entry)
    materials = {}
    for entry in entries:
        if 'box' not in entry:
            continue
        texture = entry['box'].get('texture', '')
        if texture not in materials:
            material = bpy.data.materials.new(texture or 'Plain')
            material.use_nodes = True
            bsdf = material.node_tree.nodes.get('Principled BSDF')
            bsdf.inputs['Roughness'].default_value = 0.5
            if texture:
                node = material.node_tree.nodes.new('ShaderNodeTexImage')
                node.image = bpy.data.images.load(str(assets / texture), check_existing=True)
                material.node_tree.links.new(node.outputs['Color'], bsdf.inputs['Base Color'])
            materials[texture] = material
        bpy.ops.mesh.primitive_cube_add()
        obj = bpy.context.object
        obj.name = entry['name']
        obj.matrix_world = conversion @ world(entry) @ Matrix.Diagonal((*entry['box']['halfExtents'], 1)) @ inverse
        obj.data.materials.append(materials[texture])
        # Match CreateBox: each face receives the complete texture instead of Blender's cube atlas.
        for poly in obj.data.polygons:
            for loop, uv in zip(poly.loop_indices, [(0, 0), (1, 0), (1, 1), (0, 1)]):
                obj.data.uv_layers.active.data[loop].uv = uv
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 24
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.world.use_nodes = True
    background = scene.world.node_tree.nodes.get('Background')
    background.inputs['Color'].default_value = (0.3, 0.34, 0.4, 1)
    background.inputs['Strength'].default_value = 0.5
    for name, position, energy, color, size in [
        ('Kitchen', [3.9, 3.7, 8.8], 450, (1, .88, .72), 1.2),
        ('Living', [-1, 3.65, -2.6], 500, (1, .88, .72), 1.2),
        ('Bedroom', [-1, 3.65, 8.6], 250, (1, .88, .72), 1.2),
        ('Hall', [3, 3.6, 3.5], 180, (1, .88, .72), 1.2),
        ('Outside', [-3, 8, -12], 1200, (.72, .82, 1), 10),
    ]:
        data = bpy.data.lights.new(name, 'AREA')
        data.energy, data.color, data.shape, data.size = energy, color, 'DISK', size
        obj = bpy.data.objects.new(name, data)
        bpy.context.collection.objects.link(obj)
        obj.location = conversion @ Vector(position)
    camera = bpy.data.objects.new('PreviewCamera', bpy.data.cameras.new('PreviewCamera'))
    bpy.context.collection.objects.link(camera)
    scene.camera = camera
    for name, position, target, lens in [
        ('living', [.7, 1.95, -.12], [-1.4, 1.9, -5.5], 24),
        ('window', [-.4, 2, -3.8], [-.7, 2.05, -12], 24),
        ('entry', [3.6, 2, 3.5], [6.2, 2.1, 3.36], 24),
    ]:
        camera.location = conversion @ Vector(position)
        camera.rotation_euler = ((conversion @ Vector(target)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
        camera.data.lens = lens
        scene.render.filepath = str(root / '.cache' / ('apartment_' + name + '_balcony.png'))
        bpy.ops.render.render(write_still=True)


bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
import sys
if '--inspect' in sys.argv:
    inspect()
else:
    preview()
