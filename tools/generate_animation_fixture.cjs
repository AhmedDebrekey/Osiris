const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');

const output = path.resolve(__dirname, '../games/horror/assets/models/animation_test');
fs.mkdirSync(output, { recursive: true });
const chunks = [], views = [], accessors = [];
let byteLength = 0;
function bytes(data) {
    const padding = (4 - byteLength % 4) % 4;
    if (padding) { chunks.push(Buffer.alloc(padding)); byteLength += padding; }
    const view = views.push({ buffer: 0, byteOffset: byteLength, byteLength: data.length }) - 1;
    chunks.push(data);
    byteLength += data.length;
    return view;
}
function accessor(values, type, components, integer = false, bounds = false) {
    const buffer = Buffer.alloc(values.length * (integer ? 2 : 4));
    values.forEach((value, i) => integer ? buffer.writeUInt16LE(value, i * 2) : buffer.writeFloatLE(value, i * 4));
    const item = { bufferView: bytes(buffer), componentType: integer ? 5123 : 5126, count: values.length / components, type };
    if (bounds) {
        item.min = Array.from({ length: components }, (_, axis) => Math.min(...values.filter((_, i) => i % components === axis)));
        item.max = Array.from({ length: components }, (_, axis) => Math.max(...values.filter((_, i) => i % components === axis)));
    }
    return accessors.push(item) - 1;
}
const positions = [], normals = [], uvs = [], joints = [], weights = [], indices = [];
function box(center, half, joint) {
    const faces = [
        [[1,0,0],[0,1,0],[0,0,1]], [[-1,0,0],[0,-1,0],[0,0,1]],
        [[0,1,0],[0,0,1],[1,0,0]], [[0,-1,0],[0,0,-1],[1,0,0]],
        [[0,0,1],[1,0,0],[0,1,0]], [[0,0,-1],[-1,0,0],[0,1,0]],
    ];
    for (const [normal, right, up] of faces) {
        const first = positions.length / 3;
        for (const [x, y] of [[-1,-1],[1,-1],[1,1],[-1,1]]) {
            positions.push(...center.map((c, axis) => c + half[axis] * (normal[axis] + x * right[axis] + y * up[axis])));
            normals.push(...normal);
            uvs.push((x + 1) / 2, (y + 1) / 2);
            joints.push(joint, 0, 0, 0);
            weights.push(1, 0, 0, 0);
        }
        indices.push(first, first + 1, first + 2, first, first + 2, first + 3);
    }
}
box([0,1.14,0], [.26,.30,.13], 0);
box([0,1.62,0], [.16,.18,.15], 5);
box([-.18,.43,0], [.10,.39,.11], 1);
box([.18,.43,0], [.10,.39,.11], 2);
box([-.36,1.08,0], [.08,.28,.09], 3);
box([.36,1.08,0], [.08,.28,.09], 4);
const jointPositions = [[0,0,0],[-.18,.82,0],[.18,.82,0],[-.36,1.36,0],[.36,1.36,0],[0,1.48,0]];
const inverseBind = jointPositions.flatMap(([x,y,z]) => [1,0,0,0,0,1,0,0,0,0,1,0,-x,-y,-z,1]);
const mesh = { primitives: [{
    attributes: { POSITION: accessor(positions, 'VEC3', 3, false, true), NORMAL: accessor(normals, 'VEC3', 3),
        TEXCOORD_0: accessor(uvs, 'VEC2', 2), JOINTS_0: accessor(joints, 'VEC4', 4, true), WEIGHTS_0: accessor(weights, 'VEC4', 4) },
    indices: accessor(indices, 'SCALAR', 1, true), material: 0,
}] };
const inverseBindMatrices = accessor(inverseBind, 'MAT4', 16);
const animations = [];
for (const [name, duration, angle] of [['Idle',2,3],['Walk',1,28],['Run',.6,52]]) {
    const times = [0, duration/4, duration/2, 3*duration/4, duration];
    const input = accessor(times, 'SCALAR', 1, false, true);
    const animation = { name, samplers: [], channels: [] };
    for (let joint = 1; joint <= 5; joint++) {
        const values = times.flatMap((_, i) => {
            const radians = Math.sin(i * Math.PI / 2) * angle * (joint % 2 ? 1 : -1) * (joint === 5 ? .15 : 1) * Math.PI / 180;
            return [Math.sin(radians/2), 0, 0, Math.cos(radians/2)];
        });
        const output = accessor(values, 'VEC4', 4);
        animation.channels.push({ sampler: animation.samplers.length, target: { node: joint + 2, path: 'rotation' } });
        animation.samplers.push({ input, output, interpolation: 'LINEAR' });
    }
    animations.push(animation);
}
function crc32(buffer) {
    let crc = 0xffffffff;
    for (const byte of buffer) {
        crc ^= byte;
        for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
}
function pngChunk(name, data) {
    const body = Buffer.concat([Buffer.from(name), data]);
    const header = Buffer.alloc(4), checksum = Buffer.alloc(4);
    header.writeUInt32BE(data.length); checksum.writeUInt32BE(crc32(body));
    return Buffer.concat([header, body, checksum]);
}
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(2,0); ihdr.writeUInt32BE(2,4); ihdr[8]=8; ihdr[9]=6;
const png = Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]), pngChunk('IHDR',ihdr),
    pngChunk('IDAT', zlib.deflateSync(Buffer.from([0,255,190,90,255,60,160,180,255,0,60,160,180,255,255,190,90,255]))), pngChunk('IEND',Buffer.alloc(0))]);
const imageView = bytes(png);
const binary = Buffer.concat(chunks);
const gltf = {
    asset: { version: '2.0', generator: 'Osiris animation test fixture' }, scene: 0,
    scenes: [{ nodes: [0] }], nodes: [
        { name: 'Model', children: [1,2] }, { name: 'Body', mesh: 0, skin: 0 },
        { name: 'Root', children: [3,4,5,6,7] },
        ...jointPositions.slice(1).map((translation, i) => ({ name: ['LeftHip','RightHip','LeftShoulder','RightShoulder','Head'][i], translation })),
    ], meshes: [mesh], skins: [{ name: 'TestRig', joints: [2,3,4,5,6,7], skeleton: 2, inverseBindMatrices }],
    animations, accessors, bufferViews: views, buffers: [{ uri: 'test_character.bin', byteLength: binary.length }],
    images: [{ uri: 'test_checker.png' }], textures: [{ source: 0 }],
    materials: [{ pbrMetallicRoughness: { baseColorTexture: { index: 0 }, metallicFactor: 0, roughnessFactor: .7 } }],
};
fs.writeFileSync(path.join(output, 'test_character.gltf'), JSON.stringify(gltf,null,2)+'\n');
fs.writeFileSync(path.join(output, 'test_character.bin'), binary);
fs.writeFileSync(path.join(output, 'test_checker.png'), png);
delete gltf.buffers[0].uri;
gltf.images = [{ bufferView: imageView, mimeType: 'image/png' }];
const json = Buffer.from(JSON.stringify(gltf));
const paddedJson = Buffer.concat([json, Buffer.alloc((4-json.length%4)%4,32)]);
const paddedBinary = Buffer.concat([binary,Buffer.alloc((4-binary.length%4)%4)]);
const header = Buffer.alloc(12), jsonHeader = Buffer.alloc(8), binHeader = Buffer.alloc(8);
header.writeUInt32LE(0x46546c67,0); header.writeUInt32LE(2,4); header.writeUInt32LE(28+paddedJson.length+paddedBinary.length,8);
jsonHeader.writeUInt32LE(paddedJson.length,0); jsonHeader.writeUInt32LE(0x4e4f534a,4);
binHeader.writeUInt32LE(paddedBinary.length,0); binHeader.writeUInt32LE(0x004e4942,4);
fs.writeFileSync(path.join(output,'test_character.glb'), Buffer.concat([header,jsonHeader,paddedJson,binHeader,paddedBinary]));
console.log('Generated glTF and GLB: 144 vertices, six joints, Idle/Walk/Run, external and embedded PNG.');
