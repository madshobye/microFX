(function installRetainedApi(fx) {
  "use strict";

  const elementStates = new WeakMap();
  const groupMembers = new WeakMap();
  const groupOwners = new WeakMap();
  const groups = new WeakSet();
  const sceneOwners = new WeakMap();
  const feedCache = new Map();

  function numericHandle(value) {
    if (typeof value === "number") return value;
    if (value && typeof value.handle === "number") return value.handle;
    throw new TypeError("expected a retained element or numeric handle");
  }

  function element(handle, dimension, initial) {
    const state = Object.assign({
      dimension,
      x: 0, y: 0, z: 0,
      rx: 0, ry: 0, rz: 0,
      rotation: 0,
      scale: 1,
      enabled: true,
      sceneVisible: true
    }, initial || {});

    const object = {
      handle,
      dimension,

      position(x, y, z) {
        state.x = x;
        state.y = y;
        if (dimension === 3 && z !== undefined) state.z = z;
        applyTransform();
        return object;
      },

      move(dx, dy, dz) {
        state.x += dx;
        state.y += dy;
        if (dimension === 3) state.z += dz === undefined ? 0 : dz;
        applyTransform();
        return object;
      },

      rotation(x, y, z) {
        if (dimension === 2) {
          state.rotation = x;
        } else {
          state.rx = x;
          state.ry = y === undefined ? state.ry : y;
          state.rz = z === undefined ? state.rz : z;
        }
        applyTransform();
        return object;
      },

      rotate(dx, dy, dz) {
        if (dimension === 2) {
          state.rotation += dx;
        } else {
          state.rx += dx;
          state.ry += dy === undefined ? 0 : dy;
          state.rz += dz === undefined ? 0 : dz;
        }
        applyTransform();
        return object;
      },

      scale(value) {
        if (dimension !== 3 && state.kind !== "image") {
          throw new TypeError("scale() is available on 3D and image elements");
        }
        state.scale = value;
        if (state.kind === "image") fx._imageScale(handle, value);
        else applyTransform();
        return object;
      },

      color(value) {
        fx._color(handle, value);
        return object;
      },

      visible(value) {
        state.enabled = Boolean(value);
        applyVisibility();
        return object;
      },

      enabled(value) {
        state.enabled = Boolean(value);
        applyVisibility();
        return object;
      },

      opacity(value) {
        if (dimension !== 2) throw new TypeError("opacity() is available on 2D elements");
        fx._opacity(handle, value);
        return object;
      },

      show() {
        state.enabled = true;
        applyVisibility();
        return object;
      },

      hide() {
        state.enabled = false;
        applyVisibility();
        return object;
      },

      effect(kind, amount, scale) {
        fx._effect(handle, kind, amount === undefined ? 1 : amount,
                   scale === undefined ? 4 : scale);
        return object;
      },

      text(value) {
        fx._setText(handle, value);
        return object;
      },

      font(path) {
        if (state.kind !== "text") throw new TypeError("font() is only available on text elements");
        fx._font(handle, path === undefined || path === null ? "" : String(path));
        return object;
      }
    };

    function applyTransform() {
      if (dimension === 2) {
        fx._move(handle, state.x, state.y, state.rotation);
      } else {
        fx._transform(handle, state.x, state.y, state.z,
                      state.rx, state.ry, state.rz, state.scale);
      }
    }

    function applyVisibility() {
      fx._visible(handle, state.enabled && state.sceneVisible);
    }

    elementStates.set(object, state);
    return object;
  }

  function retainedGroup() {
    const members = [];
    const state = { x: 0, y: 0, z: 0 };
    const group = {
      add(value) {
        if (!value || !elementStates.has(value)) {
          throw new TypeError("group.add() expects a retained element");
        }
        if (groupOwners.has(value)) {
          throw new Error("retained element already belongs to a group");
        }
        groupOwners.set(value, group);
        members.push(value);
        if (state.x !== 0 || state.y !== 0 || state.z !== 0) {
          value.move(state.x, state.y, state.z);
        }
        return value;
      },

      position(x, y, z) {
        const nextX = Number(x);
        const nextY = Number(y);
        const nextZ = z === undefined ? state.z : Number(z);
        if (!Number.isFinite(nextX) || !Number.isFinite(nextY) ||
            !Number.isFinite(nextZ)) {
          throw new TypeError("group.position() expects finite coordinates");
        }
        return group.move(nextX - state.x, nextY - state.y, nextZ - state.z);
      },

      move(dx, dy, dz) {
        const x = Number(dx);
        const y = Number(dy);
        const z = dz === undefined ? 0 : Number(dz);
        if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) {
          throw new TypeError("group.move() expects finite coordinates");
        }
        state.x += x;
        state.y += y;
        state.z += z;
        members.forEach(value => value.move(x, y, z));
        return group;
      },

      color(value) {
        members.forEach(member => member.color(value));
        return group;
      },

      visible(value) {
        members.forEach(member => member.visible(value));
        return group;
      },

      opacity(value) {
        if (members.some(member => elementStates.get(member).dimension === 3)) {
          throw new TypeError("group opacity requires only 2D elements");
        }
        members.forEach(member => member.opacity(value));
        return group;
      },

      show() { return group.visible(true); },
      hide() { return group.visible(false); },
      elements() { return members.slice(); }
    };
    groups.add(group);
    groupMembers.set(group, members);
    return group;
  }

  function make2d(nativeCall, args, initial) {
    return element(nativeCall.apply(fx, args), 2, initial);
  }

  function make3d(nativeCall, args, initial) {
    return element(nativeCall.apply(fx, args), 3, initial);
  }

  fx.rect = function rect(x, y, width, height, color) {
    return make2d(fx._rect, arguments, { x, y });
  };
  fx.line = function line(x1, y1, x2, y2, width, color) {
    const dx = x2 - x1;
    const dy = y2 - y1;
    const value = fx.rect((x1 + x2) * 0.5, (y1 + y2) * 0.5,
                          Math.hypot(dx, dy), width, color);
    return value.rotation(Math.atan2(dy, dx));
  };
  fx.polyline = function polyline(points, width, color, options) {
    if (!Array.isArray(points) || points.length < 2) {
      throw new TypeError("polyline() requires at least two points");
    }
    const normalized = points.map(point => {
      const x = Number(Array.isArray(point) ? point[0] : point && point.x);
      const y = Number(Array.isArray(point) ? point[1] : point && point.y);
      if (!Number.isFinite(x) || !Number.isFinite(y)) {
        throw new TypeError("polyline points require finite x and y coordinates");
      }
      return { x, y };
    });
    const closed = options === true || Boolean(options && options.closed);
    const pairs = [];
    for (let index = 1; index < normalized.length; index++) {
      const previous = normalized[index - 1];
      const current = normalized[index];
      if (previous.x !== current.x || previous.y !== current.y) {
        pairs.push([previous, current]);
      }
    }
    const first = normalized[0];
    const last = normalized[normalized.length - 1];
    if (closed && (first.x !== last.x || first.y !== last.y)) pairs.push([last, first]);
    if (!pairs.length) throw new RangeError("polyline requires a non-zero segment");
    const path = retainedGroup();
    pairs.forEach(pair => path.add(
      fx.line(pair[0].x, pair[0].y, pair[1].x, pair[1].y, width, color)));
    return path;
  };
  fx.gradientRect = function gradientRect(x, y, width, height, top, bottom) {
    return make2d(fx._gradientRect, arguments, { x, y });
  };
  fx.background = function background(top, bottom) {
    return make2d(fx._background, arguments, {});
  };
  fx.circle = function circle(x, y, radius, color) {
    return make2d(fx._circle, arguments, { x, y });
  };
  fx.sdfCircle = function sdfCircle(x, y, radius, color) {
    return make2d(fx._sdfCircle, arguments, { x, y });
  };
  fx.sdfRoundedRect = function sdfRoundedRect(x, y, width, height, radius, color) {
    return make2d(fx._sdfRoundedRect, arguments, { x, y });
  };
  fx.text = function text(value, x, y, size, color, fontPath) {
    const result = element(fx._text(value, x, y, size, color), 2,
                           { x, y, kind: "text" });
    return fontPath === undefined ? result : result.font(fontPath);
  };
  fx.image = function image(path, x, y, scale, tint) {
    if (arguments.length !== 5) {
      throw new TypeError("image(path, x, y, scale, tint) requires exactly 5 arguments");
    }
    return make2d(fx._image, arguments, { x, y, scale, kind: "image" });
  };

  fx.group = function group() {
    const result = retainedGroup();
    Array.prototype.forEach.call(arguments, value => result.add(value));
    return result;
  };

  fx.cube = function cube(x, y, z, size, color) {
    return make3d(fx._cube, arguments, { x, y, z, scale: size });
  };
  fx.sphere = function sphere(x, y, z, size, color) {
    return make3d(fx._sphere, arguments, { x, y, z, scale: size });
  };
  fx.wireCube = function wireCube(x, y, z, size, color) {
    return make3d(fx._wireCube, arguments, { x, y, z, scale: size });
  };
  fx.grid = function grid(x, y, z, size, color) {
    return make3d(fx._grid, arguments, { x, y, z, scale: size });
  };
  fx.model = function model(path, x, y, z, size, color) {
    return make3d(fx._model, arguments, { x, y, z, scale: size });
  };

  // Compatibility at the operation level: these accept either the retained
  // object returned by constructors or its numeric handle.
  fx.move = function move(target, x, y, rotation) {
    return fx._move(numericHandle(target), x, y, rotation);
  };
  fx.transform = function transform(target, x, y, z, rx, ry, rz, scale) {
    return fx._transform(numericHandle(target), x, y, z, rx, ry, rz, scale);
  };
  fx.setText = function setText(target, value) {
    return fx._setText(numericHandle(target), value);
  };
  fx.font = function font(target, path) {
    return fx._font(numericHandle(target),
                    path === undefined || path === null ? "" : String(path));
  };
  fx.color = function color(target, value) {
    return fx._color(numericHandle(target), value);
  };
  fx.visible = function visible(target, value) {
    if (target && elementStates.has(target)) return target.visible(value);
    return fx._visible(numericHandle(target), Boolean(value));
  };
  fx.opacity = function opacity(target, value) {
    return fx._opacity(numericHandle(target), value);
  };
  fx.effect = function effect(target, kind, amount, scale) {
    return fx._effect(numericHandle(target), kind,
                      amount === undefined ? 1 : amount,
                      scale === undefined ? 4 : scale);
  };

  fx.rgba = function rgba(red, green, blue, alpha) {
    const byte = value => Math.max(0, Math.min(255, Math.round(value)));
    return (((byte(red) << 24) | (byte(green) << 16) |
             (byte(blue) << 8) | byte(alpha === undefined ? 255 : alpha)) >>> 0);
  };

  // Network ownership stays in the platform adapter. A feed is read once per
  // renderer activation, so calling this helper from update() cannot turn into
  // a per-frame file read or HTTP request. Changed live data reloads the active
  // project after the adapter's bounded refresh interval.
  fx.feed = function feed(path, fallback) {
    const key = String(path);
    if (feedCache.has(key)) return feedCache.get(key);
    const value = arguments.length > 1 ? fx.data(key, fallback) : fx.data(key);
    feedCache.set(key, value);
    return value;
  };

  fx.scene = function scene(options) {
    const members = [];
    const flattened = [];
    const state = { active: false, requested: false };
    const object = {
      name: options && options.name ? String(options.name) : "scene",
      add(value) {
        if (!value || (!elementStates.has(value) && !groups.has(value))) {
          throw new TypeError("scene.add() expects a retained element or group");
        }
        const additions = groups.has(value) ? groupMembers.get(value) : [value];
        additions.forEach(member => {
          if (sceneOwners.has(member)) throw new Error("retained element already belongs to a scene");
        });
        additions.forEach(member => {
          sceneOwners.set(member, object);
          const memberState = elementStates.get(member);
          memberState.sceneVisible = false;
          fx._visible(member.handle, false);
          flattened.push(member);
        });
        members.push(value);
        return value;
      },
      show() {
        state.requested = true;
        return object;
      },
      hide() {
        state.requested = false;
        return object;
      },
      elements() {
        return members.slice();
      },
      flattenedElements() {
        return flattened.slice();
      }
    };
    Object.defineProperty(object, "_sceneState", { value: state });
    return object;
  };

  const scenes = [];
  fx.scenes = {
    add(value) {
      if (!value || typeof value.add !== "function") {
        throw new TypeError("fx.scenes.add() expects a scene");
      }
      scenes.push(value);
      return value;
    },
    all() {
      return scenes.slice();
    }
  };

  fx._beginFrame = function beginFrame() {
    scenes.forEach(scene => { scene._sceneState.requested = false; });
  };

  fx._endFrame = function endFrame() {
    scenes.forEach(scene => {
      const state = scene._sceneState;
      if (state.active === state.requested) return;
      state.active = state.requested;
      scene.flattenedElements().forEach(member => {
        const memberState = elementStates.get(member);
        memberState.sceneVisible = state.active;
        fx._visible(member.handle, memberState.enabled && memberState.sceneVisible);
      });
    });
  };
})(fx);
