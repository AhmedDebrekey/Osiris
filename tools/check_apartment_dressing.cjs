const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const assets = path.join(root, 'games/horror/assets');
const scene = JSON.parse(fs.readFileSync(path.join(assets, 'scenes/apartment.json'), 'utf8'));
const entries = scene.entities;
const named = name => {
    const matches = entries.filter(e => e.name === name);
    assert.equal(matches.length, 1, name + ' must resolve uniquely');
    return matches[0];
};
const exists = relative => assert(fs.existsSync(path.resolve(assets, relative)), 'Missing asset: ' + relative);

for (const entry of entries) {
    for (const reference of [entry.mesh, entry.modelChild, entry.box?.texture,
                             entry.script?.path, entry.audioSource?.clipPath]) {
        if (reference) exists(reference);
    }
    if (entry.parent) assert(entries.some(e => e.name === entry.parent), 'Missing parent: ' + entry.parent);
    for (const value of Object.values(entry.transform || {})) {
        assert(value.length === 3 && value.every(Number.isFinite), entry.name + ' has an invalid transform');
    }
}

const newGroups = new Set(['Livingroom_Balcony', 'Livingroom_BalconyDoor', 'Livingroom_Window', 'Apartment_OutdoorView']);
const dressing = entries.filter(e => newGroups.has(e.name) || newGroups.has(e.parent) || /^(FrontDoor_Header|Book_\d_SpineBand_)/.test(e.name));
for (const entry of dressing) {
    named(entry.name);
    const visited = new Set([entry.name]);
    let parent = entry.parent;
    while (parent) {
        assert(!visited.has(parent), 'Parent cycle for ' + entry.name);
        visited.add(parent);
        parent = named(parent).parent;
    }
}

for (const [index, color] of ['blue', 'green', 'red'].entries()) {
    for (const part of ['Cover_-1', 'Cover_1', 'Spine']) {
        assert.equal(named(`Book_${index + 1}_${part}`).box.texture, `textures/books/cloth_${color}.png`);
    }
}

for (const name of ['Livingroom_BalconyDoor', 'Livingroom_Window', 'Balcony_Floor', 'BalconyDoor_HeaderWall', 'FrontDoor_HeaderWall']) {
    const entity = named(name);
    assert.equal(entity.rigidBody.motionType, 'Static');
    assert(!entity.rigidBody.isSensor);
    assert(entity.collider.halfExtents.every(v => v > 0));
}

const frontHeader = named('FrontDoor_HeaderWall');
assert(frontHeader.transform.position[1] - frontHeader.box.halfExtents[1] <= 2.805);
assert(frontHeader.transform.position[1] + frontHeader.box.halfExtents[1] >= 3.985);
const balconyHeader = named('BalconyDoor_HeaderWall');
assert(balconyHeader.transform.position[1] + balconyHeader.box.halfExtents[1] >= 3.985);

const glass = JSON.parse(fs.readFileSync(path.join(assets, 'models/apartment/windows/glass_pane.gltf'), 'utf8'));
assert.equal(glass.materials[0].alphaMode, 'BLEND');
assert.equal(glass.materials[0].doubleSided, true);
assert(glass.materials[0].pbrMetallicRoughness.baseColorFactor[3] < .2);
assert.equal(glass.materials[0].pbrMetallicRoughness.metallicFactor, 0);
const buffer = Buffer.from(glass.buffers[0].uri.split(',')[1], 'base64');
assert.equal(buffer.length, glass.buffers[0].byteLength);
for (const view of glass.bufferViews) assert(view.byteOffset + view.byteLength <= buffer.length);
assert.equal(glass.accessors[0].count, 4);
assert.equal(glass.accessors[3].count, 6);
assert.equal(entries.filter(e => e.mesh === 'models/apartment/windows/glass_pane.gltf').length, 6);

const fill = named('Courtyard_FacadeFill');
assert.equal(fill.spotLight.castsShadow, false);
assert(entries.filter(e => e.spotLight && e.spotLight.enabled !== false).length <= 8);

console.log(`Apartment dressing checks passed: ${entries.length} entries, ${dressing.length} new dressing entries, six glass panes, three book textures, and all asset paths resolved.`);
