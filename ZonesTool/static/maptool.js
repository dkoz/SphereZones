const MAP_CONFIGS = {
    mainworld: {
        name: 'Palpagos Islands',
        landscape: [349400, 724400, -1099400, -724400],
        center: [-128, 128],
        tilePath: '/tiles/mainworld/{z}/{x}/{y}.png'
    },
    tree: {
        name: 'World Tree',
        landscape: [689148.5, -476400.0, 347351.5, -818197.0],
        center: [-128, 128],
        tilePath: '/tiles/tree/{z}/{x}/{y}.png'
    }
};

const TILE_BOUNDS = [[0, 0], [-256, 256]];
let currentMapKey = 'mainworld';
let currentConfig = MAP_CONFIGS[currentMapKey];

function mapToWorld(lat, lng) {
    var wx = (((lat + 256) * (currentConfig.landscape[0] - currentConfig.landscape[2])) / 256 + currentConfig.landscape[2]);
    var wy = ((lng * (currentConfig.landscape[1] - currentConfig.landscape[3])) / 256 + currentConfig.landscape[3]);
    return { x: wx, y: wy };
}

function worldToMap(wx, wy) {
    var lat = ((wx - currentConfig.landscape[2]) / (currentConfig.landscape[0] - currentConfig.landscape[2])) * 256 - 256;
    var lng = ((wy - currentConfig.landscape[3]) / (currentConfig.landscape[1] - currentConfig.landscape[3])) * 256;
    return [lat, lng];
}

const instanceTypes = {
    Players: { key: 'Player', label: 'Players', categories: { world: ['Build', 'Dismantle', 'EditSign', 'GroundMount', 'FlyingMount'], damage: ['Player', 'Companion', 'BaseCampPal', 'WildPal', 'NPC', 'Structure'] } },
    WildPals: { key: 'WildPal', label: 'Wild Pals', categories: { damage: ['Player', 'Companion', 'BaseCampPal', 'WildPal', 'NPC', 'Structure'] } },
    Npcs: { key: 'NPC', label: 'NPCs', categories: { damage: ['Player', 'Companion', 'BaseCampPal', 'WildPal', 'NPC', 'Structure'] } },
    PlayersPals: { key: 'Companion', label: 'Player Pals', categories: { damage: ['Player', 'Companion', 'BaseCampPal', 'WildPal', 'NPC', 'Structure'] } },
    BasePals: { key: 'BaseCampPal', label: 'Base Pals', categories: { damage: ['Player', 'Companion', 'BaseCampPal', 'WildPal', 'NPC', 'Structure'] } }
};

const categoryLabels = {
    world: 'Non-Admin Can:',
    damage: 'Can Damage:'
};

const valueLabels = {
    Build: 'Build Structures',
    Dismantle: 'Dismantle Structures',
    EditSign: 'Edit Signs',
    GroundMount: 'Use Ground Mounts',
    FlyingMount: 'Use Flying Mounts',
    Player: 'Players',
    Companion: 'Players Held Pals',
    BaseCampPal: 'Players Base Pals',
    WildPal: 'Wild Pals',
    NPC: 'NPCs',
    Structure: 'Structures'
};

const statusEffects = [
    { id: 5, name: 'Poison' },
    { id: 19, name: 'Burn' },
    { id: 7, name: 'Stun' },
    { id: 21, name: 'Freeze' },
    { id: 22, name: 'Electrical' },
    { id: 23, name: 'Muddy' },
    { id: 24, name: 'IvyCling' },
    { id: 25, name: 'Darkness' },
    { id: 14, name: 'Drown' },
    { id: 18, name: 'LavaDamage' },
    { id: 43, name: 'ToxicGas' },
    { id: 40, name: 'AcidDamage' },
    { id: 45, name: 'Stealth' },
    { id: 47, name: 'LowGravity' },
    { id: 15, name: 'Dying' },
    { id: 26, name: 'AttackUp' },
    { id: 27, name: 'DefenseUp' },
    { id: 28, name: 'AttackDown' },
    { id: 30, name: 'LifeSteal' },
    { id: 32, name: 'RarePalEffect' },
    { id: 39, name: 'HPLock' },
    { id: 77, name: 'FloatStun' }
];

let zoneIdCounter = 0;
let drawnItems = new L.FeatureGroup();
let activeZoneId = null;
let isGlobalPanel = false;
let zoneData = {};
let globalPermissions = {};

const map = L.map('map', {
    crs: L.CRS.Simple,
    minZoom: 1,
    maxZoom: 6,
    center: currentConfig.center,
    zoom: 2,
    zoomControl: true,
    attributionControl: false,
    preferCanvas: true,
    zoomAnimation: true,
    markerZoomAnimation: true,
    fadeAnimation: false,
    zoomSnap: 0.5,
    wheelDebounceTime: 40,
    inertia: false
});

let currentTileLayer = L.tileLayer(currentConfig.tilePath, {
    noWrap: true,
    bounds: TILE_BOUNDS,
    maxNativeZoom: 5,
    keepBuffer: 4,
    updateWhenIdle: true,
    updateWhenZooming: false,
    crossOrigin: true
}).addTo(map);

map.addLayer(drawnItems);

const drawControl = new L.Control.Draw({
    position: 'topright',
    edit: {
        featureGroup: drawnItems,
        remove: false
    },
    draw: {
        polygon: {
            allowIntersection: false,
            showArea: false,
            shapeOptions: {
                color: '#00a2ff',
                fillColor: '#00a2ff',
                fillOpacity: 0.2
            }
        },
        polyline: false,
        rectangle: false,
        circle: false,
        circlemarker: false,
        marker: false
    }
});
map.addControl(drawControl);

// Relocate the draw and edit buttons into the map switcher bar. Moving the
// control's own DOM keeps every leaflet-draw handler and its action bar wired
// up, which rebuilding the buttons by hand would not.
(function moveDrawToolsIntoSwitcher() {
    var slot = document.getElementById('draw-tools');
    var control = document.querySelector('.leaflet-draw.leaflet-control');
    if (!slot || !control) return;
    slot.appendChild(control);
})();

let coordRaf = null;
let coordPending = null;

function flushCoords() {
    coordRaf = 0;
    if (!coordPending) return;
    var lat = coordPending.lat;
    var lng = coordPending.lng;
    coordPending = null;
    var w = mapToWorld(lat, lng);
    document.getElementById('coord-x').textContent = Math.round(w.x);
    document.getElementById('coord-y').textContent = Math.round(w.y);
}

map.on('mousemove', function(e) {
    coordPending = e.latlng;
    if (!coordRaf) coordRaf = requestAnimationFrame(flushCoords);
});

var dragLayer = null;
var dragStartLatLngs = null;
var dragStartMouse = null;
var editModeActive = false;

map.on(L.Draw.Event.EDITSTART, function() { editModeActive = true; });
map.on(L.Draw.Event.EDITSTOP, function() { editModeActive = false; });

function deepCopyLatLngs(rings) {
    var copy = [];
    for (var i = 0; i < rings.length; i++) {
        if (Array.isArray(rings[i]) && !rings[i].lat) {
            var ring = [];
            for (var j = 0; j < rings[i].length; j++) {
                if (Array.isArray(rings[i][j]) && !rings[i][j].lat) {
                    var subring = [];
                    for (var k = 0; k < rings[i][j].length; k++) {
                        subring.push({lat: rings[i][j][k].lat, lng: rings[i][j][k].lng});
                    }
                    ring.push(subring);
                } else {
                    ring.push({lat: rings[i][j].lat, lng: rings[i][j].lng});
                }
            }
            copy.push(ring);
        } else {
            copy.push({lat: rings[i].lat, lng: rings[i].lng});
        }
    }
    return copy;
}

function applyDragOffset(layer, startLatLngs, dLat, dLng) {
    var newLatLngs = [];
    for (var i = 0; i < startLatLngs.length; i++) {
        if (Array.isArray(startLatLngs[i])) {
            var ring = [];
            for (var j = 0; j < startLatLngs[i].length; j++) {
                if (Array.isArray(startLatLngs[i][j]) && !startLatLngs[i][j].lat) {
                    var subring = [];
                    for (var k = 0; k < startLatLngs[i][j].length; k++) {
                        subring.push([startLatLngs[i][j][k].lat + dLat, startLatLngs[i][j][k].lng + dLng]);
                    }
                    ring.push(subring);
                } else {
                    ring.push([startLatLngs[i][j].lat + dLat, startLatLngs[i][j].lng + dLng]);
                }
            }
            newLatLngs.push(ring);
        } else {
            newLatLngs.push([startLatLngs[i].lat + dLat, startLatLngs[i].lng + dLng]);
        }
    }
    layer.setLatLngs(newLatLngs);
}

function makeDraggable(layer) {
    layer.on('mousedown', function(e) {
        if (!editModeActive) return;
        if (e.originalEvent.shiftKey) return;
        if (e.originalEvent.target && e.originalEvent.target.classList && e.originalEvent.target.classList.contains('leaflet-editing-icon')) return;
        dragLayer = layer;
        dragStartMouse = e.latlng;
        dragStartLatLngs = deepCopyLatLngs(layer.getLatLngs());
        map.dragging.disable();
        L.DomEvent.preventDefault(e.originalEvent);
    });
}

map.on('mousemove', function(e) {
    if (!dragLayer) return;
    var dLat = e.latlng.lat - dragStartMouse.lat;
    var dLng = e.latlng.lng - dragStartMouse.lng;
    applyDragOffset(dragLayer, dragStartLatLngs, dLat, dLng);
    if (dragLayer.editing && dragLayer.editing._enabled) {
        var rings = dragLayer.getLatLngs();
        dragLayer.editing.latlngs = rings;
        if (dragLayer.editing._verticesHandlers) {
            for (var i = 0; i < dragLayer.editing._verticesHandlers.length; i++) {
                dragLayer.editing._verticesHandlers[i]._latlngs = rings[i];
            }
        }
        dragLayer.editing.updateMarkers();
    }
});

map.on('mouseup', function() {
    if (!dragLayer) return;
    if (dragLayer.editing && dragLayer.editing._enabled) {
        var rings = dragLayer.getLatLngs();
        dragLayer.editing.latlngs = rings;
        if (dragLayer.editing._verticesHandlers) {
            for (var i = 0; i < dragLayer.editing._verticesHandlers.length; i++) {
                dragLayer.editing._verticesHandlers[i]._latlngs = rings[i];
            }
        }
        dragLayer.editing.updateMarkers();
    }
    dragLayer = null;
    dragStartLatLngs = null;
    map.dragging.enable();
});

map.on(L.Draw.Event.CREATED, function(e) {
    const layer = e.layer;
    const zoneId = String(zoneIdCounter);
    layer.zoneId = zoneId;
    drawnItems.addLayer(layer);
    makeDraggable(layer);

    zoneData[zoneId] = {
        name: 'Default Zone ' + (zoneIdCounter + 1),
        layer: layer,
        permissions: defaultPermissions(),
        addStatus: [],
        locked: false,
        minimumLevel: 0
    };

    zoneIdCounter++;
    updateZoneList();
    selectZone(zoneId);
});

map.on(L.Draw.Event.EDITED, function(e) {
    const layers = e.layers;
    layers.eachLayer(function(layer) {
        if (layer.zoneId && zoneData[layer.zoneId]) {
            zoneData[layer.zoneId].layer = layer;
        }
    });
});

function defaultPermissions() {
    const perms = {};
    for (const [tab, config] of Object.entries(instanceTypes)) {
        perms[config.key] = {};
        for (const [cat, vals] of Object.entries(config.categories)) {
            perms[config.key][cat] = [...vals];
        }
    }
    return perms;
}

function selectZone(zoneId) {
    activeZoneId = zoneId;
    isGlobalPanel = false;
    showPermissionPanel(zoneData[zoneId].name, zoneData[zoneId].permissions, false);
    highlightZoneInList(zoneId);
}

function selectGlobal() {
    activeZoneId = null;
    isGlobalPanel = true;
    if (!globalPermissions || Object.keys(globalPermissions).length === 0) {
        globalPermissions = defaultPermissions();
    }
    showPermissionPanel('Global Permissions', globalPermissions, true);
    highlightZoneInList(null);
}

function highlightZoneInList(zoneId) {
    document.querySelectorAll('.zone-list-item').forEach(function(item) {
        item.classList.toggle('active', item.dataset.zoneId === zoneId);
    });
}

function showPermissionPanel(title, permissions, isGlobal) {
    document.getElementById('panel-title').textContent = title;
    document.getElementById('zone-name-row').classList.toggle('hidden', isGlobal);
    if (!isGlobal) {
        document.getElementById('zone-name-input').value = title;
    }
    document.getElementById('btn-delete-zone').classList.toggle('hidden', isGlobal);
    document.getElementById('permission-panel').classList.remove('hidden');

    var addStatus = [];
    if (!isGlobal && activeZoneId && zoneData[activeZoneId]) {
        addStatus = zoneData[activeZoneId].addStatus || [];
    }

    buildPermissionTabs(permissions, isGlobal, addStatus);
    document.querySelector('.nav-link.active').click();
}

function buildPermissionTabs(permissions, isGlobal, addStatus) {
    const container = document.getElementById('perm-tabs');
    container.innerHTML = '';

    for (const [tab, config] of Object.entries(instanceTypes)) {
        const tabContent = document.createElement('div');
        tabContent.className = 'tab-pane';
        tabContent.dataset.tab = tab;

        const inst = document.createElement('div');
        inst.className = 'instanceType';
        inst.dataset.key = config.key;

        for (const [cat, vals] of Object.entries(config.categories)) {
            const catDiv = document.createElement('div');
            catDiv.className = 'category';
            catDiv.dataset.key = cat;

            const label = document.createElement('span');
            label.textContent = categoryLabels[cat] || cat;
            catDiv.appendChild(label);

            vals.forEach(function(val) {
                const check = document.createElement('div');
                check.className = 'form-check form-switch';

                const input = document.createElement('input');
                input.className = 'form-check-input';
                input.type = 'checkbox';
                input.role = 'switch';
                input.dataset.instance = config.key;
                input.dataset.category = cat;
                input.dataset.value = val;
                input.checked = permissions[config.key] && permissions[config.key][cat] && permissions[config.key][cat].includes(val);

                input.addEventListener('change', function() {
                    saveCurrentPermissions();
                });

                const lbl = document.createElement('label');
                lbl.className = 'form-check-label';
                lbl.textContent = valueLabels[val] || val;

                check.appendChild(input);
                check.appendChild(lbl);
                catDiv.appendChild(check);
            });

            inst.appendChild(catDiv);
        }

        tabContent.appendChild(inst);
        container.appendChild(tabContent);
    }

    var statusTab = document.createElement('div');
    statusTab.className = 'tab-pane';
    statusTab.dataset.tab = 'StatusEffects';

    if (isGlobal) {
        var msg = document.createElement('p');
        msg.textContent = 'Status effects are only available per-zone, not globally.';
        msg.style.color = '#9ca3af';
        msg.style.fontSize = '14px';
        statusTab.appendChild(msg);
    } else {
        var addSection = document.createElement('div');
        addSection.className = 'category';
        var addLabel = document.createElement('span');
        addLabel.textContent = 'Add Status on Enter:';
        addSection.appendChild(addLabel);

        statusEffects.forEach(function(effect) {
            var check = document.createElement('div');
            check.className = 'form-check form-switch';
            var input = document.createElement('input');
            input.className = 'form-check-input';
            input.type = 'checkbox';
            input.role = 'switch';
            input.dataset.statusType = 'add';
            input.dataset.statusId = effect.id;
            input.checked = addStatus.includes(effect.id);
            input.addEventListener('change', function() { saveCurrentStatus(); });
            var lbl = document.createElement('label');
            lbl.className = 'form-check-label';
            lbl.textContent = effect.name + ' (' + effect.id + ')';
            check.appendChild(input);
            check.appendChild(lbl);
            addSection.appendChild(check);
        });
        statusTab.appendChild(addSection);
    }

    container.appendChild(statusTab);

    var accessTab = document.createElement('div');
    accessTab.className = 'tab-pane';
    accessTab.dataset.tab = 'Access';

    if (isGlobal) {
        var accessMsg = document.createElement('p');
        accessMsg.textContent = 'Entry locks are only available per-zone, not globally.';
        accessMsg.style.color = '#9ca3af';
        accessMsg.style.fontSize = '14px';
        accessTab.appendChild(accessMsg);
    } else {
        var zone = zoneData[activeZoneId] || {};

        var lockSection = document.createElement('div');
        lockSection.className = 'category';
        var lockLabel = document.createElement('span');
        lockLabel.textContent = 'Entry Lock:';
        lockSection.appendChild(lockLabel);

        var lockCheck = document.createElement('div');
        lockCheck.className = 'form-check form-switch';
        var lockInput = document.createElement('input');
        lockInput.className = 'form-check-input';
        lockInput.type = 'checkbox';
        lockInput.role = 'switch';
        lockInput.dataset.access = 'locked';
        lockInput.checked = zone.locked === true;
        lockInput.addEventListener('change', function() { saveCurrentAccess(); });
        var lockText = document.createElement('label');
        lockText.className = 'form-check-label';
        lockText.textContent = 'Locked (nobody may enter)';
        lockCheck.appendChild(lockInput);
        lockCheck.appendChild(lockText);
        lockSection.appendChild(lockCheck);

        accessTab.appendChild(lockSection);

        var levelSection = document.createElement('div');
        levelSection.className = 'category';
        var levelLabel = document.createElement('span');
        levelLabel.textContent = 'Minimum Level:';
        levelSection.appendChild(levelLabel);

        var levelInput = document.createElement('input');
        levelInput.type = 'number';
        levelInput.className = 'form-control';
        levelInput.min = '0';
        levelInput.max = '999';
        levelInput.placeholder = '0 for no level requirement';
        levelInput.dataset.access = 'minimumLevel';
        levelInput.value = zone.minimumLevel > 0 ? zone.minimumLevel : '';
        levelInput.addEventListener('input', function() { saveCurrentAccess(); });
        levelSection.appendChild(levelInput);

        var levelHint = document.createElement('label');
        levelHint.className = 'form-check-label';
        levelHint.textContent = 'Players below this level are turned away. Blank or 0 means no requirement.';
        levelSection.appendChild(levelHint);

        accessTab.appendChild(levelSection);

        var adminNote = document.createElement('p');
        adminNote.textContent = 'Server admins pass through both.';
        adminNote.style.color = '#9ca3af';
        adminNote.style.fontSize = '13px';
        adminNote.style.marginTop = '10px';
        accessTab.appendChild(adminNote);
    }

    container.appendChild(accessTab);
}

function saveCurrentAccess() {
    if (!activeZoneId || !zoneData[activeZoneId]) return;

    var scope = '#perm-tabs .tab-pane[data-tab="Access"] ';
    var lockInput = document.querySelector(scope + 'input[data-access="locked"]');
    var levelInput = document.querySelector(scope + 'input[data-access="minimumLevel"]');

    if (lockInput) {
        zoneData[activeZoneId].locked = lockInput.checked;
    }
    if (levelInput) {
        var parsed = parseInt(levelInput.value, 10);
        zoneData[activeZoneId].minimumLevel = (isNaN(parsed) || parsed < 1) ? 0 : parsed;
    }
}

function saveCurrentPermissions() {
    const perms = {};
    document.querySelectorAll('#perm-tabs .instanceType').forEach(function(inst) {
        const instKey = inst.dataset.key;
        if (!perms[instKey]) perms[instKey] = {};
        inst.querySelectorAll('.category').forEach(function(cat) {
            const catKey = cat.dataset.key;
            if (!perms[instKey][catKey]) perms[instKey][catKey] = [];
            cat.querySelectorAll('.form-check-input').forEach(function(input) {
                if (input.checked) {
                    perms[instKey][catKey].push(input.dataset.value);
                }
            });
        });
    });

    if (isGlobalPanel) {
        globalPermissions = perms;
    } else if (activeZoneId && zoneData[activeZoneId]) {
        zoneData[activeZoneId].permissions = perms;
        const nameInput = document.getElementById('zone-name-input');
        if (nameInput) {
            zoneData[activeZoneId].name = nameInput.value || 'Default Zone';
            updateZoneList();
        }
    }
}

function saveCurrentStatus() {
    if (!activeZoneId || !zoneData[activeZoneId]) return;
    var addStatus = [];
    document.querySelectorAll('#perm-tabs .tab-pane[data-tab="StatusEffects"] .form-check-input').forEach(function(input) {
        if (input.checked) {
            addStatus.push(parseInt(input.dataset.statusId));
        }
    });
    zoneData[activeZoneId].addStatus = addStatus;
}

function updateZoneList() {
    const list = document.getElementById('zone-list');
    list.innerHTML = '';

    Object.keys(zoneData).forEach(function(zoneId) {
        const item = document.createElement('div');
        item.className = 'zone-list-item';
        item.dataset.zoneId = zoneId;
        item.textContent = zoneData[zoneId].name;
        item.addEventListener('click', function() {
            selectZone(zoneId);
        });
        list.appendChild(item);
    });
}

document.querySelectorAll('.nav-link').forEach(function(tab) {
    tab.addEventListener('click', function() {
        document.querySelectorAll('.nav-link').forEach(function(t) { t.classList.remove('active'); });
        tab.classList.add('active');
        document.querySelectorAll('.tab-pane').forEach(function(p) { p.classList.remove('active'); });
        document.querySelector(`.tab-pane[data-tab="${tab.dataset.tab}"]`).classList.add('active');
    });
});

document.getElementById('panel-close').addEventListener('click', function() {
    saveCurrentPermissions();
    saveCurrentStatus();
    saveCurrentAccess();
    document.getElementById('permission-panel').classList.add('hidden');
    activeZoneId = null;
    isGlobalPanel = false;
    highlightZoneInList(null);
});

document.getElementById('zone-name-input').addEventListener('input', function() {
    if (activeZoneId && zoneData[activeZoneId]) {
        zoneData[activeZoneId].name = this.value || 'Default Zone';
        updateZoneList();
    }
});

document.getElementById('btn-delete-zone').addEventListener('click', function() {
    if (activeZoneId && zoneData[activeZoneId]) {
        drawnItems.removeLayer(zoneData[activeZoneId].layer);
        delete zoneData[activeZoneId];
        updateZoneList();
        document.getElementById('permission-panel').classList.add('hidden');
        activeZoneId = null;
    }
});

document.getElementById('btn-global').addEventListener('click', function() {
    selectGlobal();
});

document.querySelectorAll('.map-switch-btn').forEach(function(btn) {
    btn.addEventListener('click', function() {
        const mapKey = btn.dataset.map;
        if (mapKey === currentMapKey) return;

        saveCurrentPermissions();
        saveCurrentStatus();
        saveCurrentAccess();

        Object.keys(zoneData).forEach(function(zoneId) {
            drawnItems.removeLayer(zoneData[zoneId].layer);
            var latlngs = zoneData[zoneId].layer.getLatLngs()[0];
            var worldPts = latlngs.map(function(ll) {
                return mapToWorld(ll.lat, ll.lng);
            });
            zoneData[zoneId]._worldPts = worldPts;
        });

        currentMapKey = mapKey;
        currentConfig = MAP_CONFIGS[mapKey];

        Object.keys(zoneData).forEach(function(id) {
            if (parseInt(id) >= zoneIdCounter) zoneIdCounter = parseInt(id) + 1;
        });

        if (currentTileLayer) {
            map.removeLayer(currentTileLayer);
        }
        currentTileLayer = L.tileLayer(currentConfig.tilePath, {
            noWrap: true,
            bounds: TILE_BOUNDS,
            maxNativeZoom: 5,
            keepBuffer: 4,
            updateWhenIdle: true,
            updateWhenZooming: false,
            crossOrigin: true
        }).addTo(map);

        map.setView(currentConfig.center, 2);

        Object.keys(zoneData).forEach(function(zoneId) {
            var z = zoneData[zoneId];
            var newLatlngs = z._worldPts.map(function(p) {
                return worldToMap(p.x, p.y);
            });
            var newPolygon = L.polygon(newLatlngs, {
                color: '#00a2ff',
                fillColor: '#00a2ff',
                fillOpacity: 0.2
            });
            newPolygon.zoneId = zoneId;
            z.layer = newPolygon;
            makeDraggable(newPolygon);
            drawnItems.addLayer(newPolygon);
        });

        document.querySelectorAll('.map-switch-btn').forEach(function(b) { b.classList.remove('active'); });
        btn.classList.add('active');

        document.getElementById('permission-panel').classList.add('hidden');
        activeZoneId = null;
        isGlobalPanel = false;

        updateZoneList();
    });
});

document.getElementById('btn-export').addEventListener('click', function() {
    saveCurrentPermissions();
    saveCurrentStatus();

    var zones = [];
    Object.keys(zoneData).forEach(function(zoneId) {
        var z = zoneData[zoneId];
        var latlngs = z.layer.getLatLngs()[0];
        var points = latlngs.map(function(ll) {
            return mapToWorld(ll.lat, ll.lng);
        });
        zones.push({
            name: z.name,
            points: points,
            locked: z.locked === true,
            minimumLevel: z.minimumLevel > 0 ? z.minimumLevel : 0,
            addStatus: z.addStatus || [],
            permissions: z.permissions
        });
    });

    // Tells the mod this file's world lists know about EditSign, GroundMount and
    // FlyingMount, so denying all three is read as intent rather than as an old
    // file that predates them.
    var data = {
        version: 2,
        global: { permissions: globalPermissions },
        zones: zones
    };

    fetch('/api/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mapKey: currentMapKey, content: data })
    }).then(r => r.json()).then(function(res) {
        if (res.error && res.error !== 'Cancelled') {
            alert('Export error: ' + res.error);
        }
    }).catch(function(err) {
        alert('Export failed: ' + err.message);
    });
});

document.getElementById('btn-import').addEventListener('click', function() {
    fetch('/api/load', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' }
    }).then(r => r.json()).then(function(res) {
        if (res.error) return;
        var imported = res.content;
        try {
            Object.keys(zoneData).forEach(function(zoneId) {
                drawnItems.removeLayer(zoneData[zoneId].layer);
            });
            zoneData = {};
            zoneIdCounter = 0;
            updateZoneList();

            if (imported.global && imported.global.permissions) {
                globalPermissions = imported.global.permissions;
            }

            if (imported.zones) {
                imported.zones.forEach(function(z) {
                    var zoneId = String(zoneIdCounter);
                    var latlngs = z.points.map(function(p) {
                        return worldToMap(p.x, p.y);
                    });
                    var polygon = L.polygon(latlngs, {
                        color: '#00a2ff',
                        fillColor: '#00a2ff',
                        fillOpacity: 0.2
                    });
                    polygon.zoneId = zoneId;
                    drawnItems.addLayer(polygon);
                    makeDraggable(polygon);
                    zoneData[zoneId] = {
                        name: z.name || 'Default Zone ' + (zoneIdCounter + 1),
                        layer: polygon,
                        permissions: z.permissions || defaultPermissions(),
                        addStatus: z.addStatus || [],
                        locked: z.locked === true,
                        minimumLevel: (typeof z.minimumLevel === 'number' && z.minimumLevel > 0) ? z.minimumLevel : 0
                    };
                    zoneIdCounter++;
                });
                updateZoneList();
            }
        } catch (err) {
            alert('Error importing file: ' + err.message);
        }
    }).catch(function(err) {
        alert('Import failed: ' + err.message);
    });
});
