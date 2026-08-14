(function installRetainedApi(fx) {
  "use strict";

  const elementStates = new WeakMap();
  const groupMembers = new WeakMap();
  const groupOwners = new WeakMap();
  const groups = new WeakSet();

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
      scale: 1
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
        if (dimension !== 3) throw new TypeError("scale() is only available on 3D elements");
        state.scale = value;
        applyTransform();
        return object;
      },

      color(value) {
        fx._color(handle, value);
        return object;
      },

      visible(value) {
        fx._visible(handle, Boolean(value));
        return object;
      },

      opacity(value) {
        if (dimension !== 2) throw new TypeError("opacity() is available on 2D elements");
        fx._opacity(handle, value);
        return object;
      },

      show() {
        fx._visible(handle, true);
        return object;
      },

      hide() {
        fx._visible(handle, false);
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
  fx.image = function image(path, x, y, width, height, tint) {
    return make2d(fx._image, arguments, { x, y });
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

  fx.scene = function scene(options) {
    const members = [];
    return {
      name: options && options.name ? String(options.name) : "scene",
      add(value) {
        if (!value || (!elementStates.has(value) && !groups.has(value))) {
          throw new TypeError("scene.add() expects a retained element or group");
        }
        members.push(value);
        return value;
      },
      elements() {
        return members.slice();
      },
      flattenedElements() {
        const flattened = [];
        members.forEach(value => {
          if (groups.has(value)) flattened.push(...groupMembers.get(value));
          else flattened.push(value);
        });
        return flattened;
      }
    };
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
})(fx);
