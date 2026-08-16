(function installRetainedApi(fx) {
  "use strict";

  const elementStates = new WeakMap();
  const groupMembers = new WeakMap();
  const groupOwners = new WeakMap();
  const groups = new WeakSet();
  const tileMaps = new WeakSet();
  const gpuTextures = new WeakSet();
  const tileMapOwners = new WeakMap();
  const activeTileMaps = [];
  const sceneOwners = new WeakMap();
  const feedCache = new Map();

  function numericHandle(value) {
    if (typeof value === "number") return value;
    if (value && typeof value.handle === "number") return value.handle;
    throw new TypeError("expected a retained element or numeric handle");
  }

  fx.geo = Object.freeze({
    sunPosition(date, latitude, longitude) {
      const instant = date instanceof Date ? date : new Date(date);
      const epoch = instant.getTime();
      const lat = Number(latitude);
      const lon = Number(longitude);
      if (!Number.isFinite(epoch) || !Number.isFinite(lat) ||
          !Number.isFinite(lon) || lat < -90 || lat > 90 ||
          lon < -180 || lon > 180) {
        throw new RangeError("sunPosition requires a valid date, latitude, and longitude");
      }
      const radians = Math.PI / 180;
      const start = Date.UTC(instant.getUTCFullYear(), 0, 0);
      const day = Math.floor((epoch - start) / 86400000);
      const minutes = instant.getUTCHours() * 60 + instant.getUTCMinutes() +
        instant.getUTCSeconds() / 60 + instant.getUTCMilliseconds() / 60000;
      const gamma = 2 * Math.PI / 365 *
        (day - 1 + (minutes / 60 - 12) / 24);
      const equation = 229.18 * (0.000075 + 0.001868 * Math.cos(gamma) -
        0.032077 * Math.sin(gamma) - 0.014615 * Math.cos(2 * gamma) -
        0.040849 * Math.sin(2 * gamma));
      const declination = 0.006918 - 0.399912 * Math.cos(gamma) +
        0.070257 * Math.sin(gamma) - 0.006758 * Math.cos(2 * gamma) +
        0.000907 * Math.sin(2 * gamma) - 0.002697 * Math.cos(3 * gamma) +
        0.00148 * Math.sin(3 * gamma);
      let solarMinutes = (minutes + equation + lon * 4) % 1440;
      if (solarMinutes < 0) solarMinutes += 1440;
      const hourAngle = (solarMinutes / 4 - 180) * radians;
      const latitudeRadians = lat * radians;
      const sinElevation = Math.max(-1, Math.min(1,
        Math.sin(latitudeRadians) * Math.sin(declination) +
        Math.cos(latitudeRadians) * Math.cos(declination) *
        Math.cos(hourAngle)));
      const elevationRadians = Math.asin(sinElevation);
      let azimuthRadians = Math.atan2(-Math.sin(hourAngle),
        Math.tan(declination) * Math.cos(latitudeRadians) -
        Math.sin(latitudeRadians) * Math.cos(hourAngle));
      if (azimuthRadians < 0) azimuthRadians += Math.PI * 2;
      return Object.freeze({
        elevationRadians,
        elevationDegrees: elevationRadians / radians,
        sinElevation,
        azimuthRadians,
        azimuthDegrees: azimuthRadians / radians,
        east: Math.sin(azimuthRadians),
        north: Math.cos(azimuthRadians),
        daylight: sinElevation > 0
      });
    }
  });

  fx.assets = Object.freeze({
    load(options) {
      const settings = options || {};
      const required = settings.required === undefined ? [] : settings.required;
      const lazy = settings.lazy === undefined ? [] : settings.lazy;
      const settleFrames = Number(settings.settleFrames === undefined ? 1 :
        settings.settleFrames);
      if (!Array.isArray(required) || !Array.isArray(lazy)) {
        throw new TypeError("assets.load requires required and lazy arrays");
      }
      if (!Number.isInteger(settleFrames) || settleFrames < 0 || settleFrames > 60) {
        throw new RangeError("assets.load settleFrames must be an integer from 0 to 60");
      }
      const resources = required.concat(lazy);
      resources.forEach(resource => {
        if (!resource || typeof resource.isReady !== "function" ||
            typeof resource.ready !== "function") {
          throw new TypeError("assets.load entries require isReady() and ready()");
        }
      });
      let loadingScene = null;
      let loadingText = null;
      let loadingLabel = "LOADING";
      if (settings.loading) {
        const loading = settings.loading === true ? {} : settings.loading;
        if (!loading || typeof loading !== "object" || Array.isArray(loading)) {
          throw new TypeError("assets.load loading must be true or an options object");
        }
        const width = Number.isFinite(Number(fx.width)) ? Number(fx.width) : 1920;
        const height = Number.isFinite(Number(fx.height)) ? Number(fx.height) : 1080;
        const x = Number(loading.x === undefined ? width * 0.5 - 155 : loading.x);
        const y = Number(loading.y === undefined ? height * 0.5 - 25 : loading.y);
        const size = Number(loading.size === undefined ? 24 : loading.size);
        const color = loading.color === undefined ? 0x777777ff : loading.color;
        if (![x, y, size].every(Number.isFinite) || size <= 0) {
          throw new RangeError("assets.load loading position and size must be finite");
        }
        loadingLabel = String(loading.label === undefined ? "LOADING" : loading.label);
        loadingScene = fx.scenes.add(fx.scene({
          name: String(loading.name === undefined ? "asset-loading" : loading.name)
        }));
        loadingText = loadingScene.add(
          fx.text(`${loadingLabel} 0 / ${required.length}`, x, y, size, color)
            .antialias(Boolean(loading.antialias)));
      }
      let settled = 0;
      const countReady = list => list.reduce((count, resource) =>
        count + (resource.isReady() ? 1 : 0), 0);
      const snapshot = () => {
        const requiredReady = countReady(required);
        const lazyReady = countReady(lazy);
        const sourcesReady = requiredReady === required.length;
        return Object.freeze({
          requiredReady,
          requiredTotal: required.length,
          lazyReady,
          lazyTotal: lazy.length,
          sourcesReady,
          ready: sourcesReady && settled >= settleFrames
        });
      };
      return Object.freeze({
        update(time) {
          if (countReady(required) === required.length) {
            if (settled < settleFrames) settled++;
          } else settled = 0;
          const state = snapshot();
          if (loadingScene) {
            if (state.ready) loadingScene.hide();
            else {
              const seconds = Number.isFinite(Number(time)) ? Number(time) : 0;
              const dots = ".".repeat(Math.floor(seconds * 3) % 4);
              loadingText.text(`${loadingLabel} ${state.requiredReady} / ` +
                `${state.requiredTotal}${dots}`);
              loadingScene.show();
            }
          }
          return state;
        },
        status: snapshot,
        ready() { return Promise.all(required.map(resource => resource.ready())); }
      });
    }
  });

  function element(handle, dimension, initial) {
    const state = Object.assign({
      dimension,
      x: 0, y: 0, z: 0,
      rx: 0, ry: 0, rz: 0,
      rotation: 0,
      scale: 1,
      enabled: true,
      sceneActive: true
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
        if (dimension !== 3 && state.kind !== "image" && state.kind !== "outline") {
          throw new TypeError("scale() is available on 3D, image, and outline elements");
        }
        state.scale = value;
        if (state.kind === "image") fx._imageScale(handle, value);
        else if (state.kind === "outline") fx._outlineScale(handle, value);
        else applyTransform();
        return object;
      },

      points(value) {
        if (state.kind !== "outline") throw new TypeError("points() is available on outlines");
        fx._outlinePoints(handle, normalizePath(value));
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

      shader(vertex, fragment) {
        if (dimension !== 3) throw new TypeError("shader() is available on 3D elements");
        if (fragment === undefined) fx._shader(handle, String(vertex));
        else fx._shader(handle, String(vertex), String(fragment));
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
      },

      antialias(enabled) {
        if (state.kind !== "text") throw new TypeError("antialias() is only available on text elements");
        fx._textAntialias(handle, Boolean(enabled));
        return object;
      },

      stage(value) {
        if (state.kind !== "sdf") {
          throw new TypeError("stage() is available on SDF elements");
        }
        const name = String(value);
        if (name !== "overlay" && name !== "foreground") {
          throw new RangeError("SDF stage must be overlay or foreground");
        }
        fx._sdfForeground(handle, name === "foreground");
        return object;
      },

      shape(kind, width, height, radius) {
        if (state.kind !== "sdf") throw new TypeError("shape() is only available on SDF elements");
        fx._sdfGeometry(handle, String(kind), Number(width), Number(height),
                        radius === undefined ? 0 : Number(radius));
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
      fx._visible(handle, state.enabled && state.sceneActive);
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
  function normalizePath(points) {
    if (!Array.isArray(points) || points.length < 2 || points.length > 64) {
      throw new TypeError("outline points require an array of 2 to 64 points");
    }
    const flat = [];
    points.forEach(point => {
      const x = Number(Array.isArray(point) ? point[0] : point && point.x);
      const y = Number(Array.isArray(point) ? point[1] : point && point.y);
      if (!Number.isFinite(x) || !Number.isFinite(y)) {
        throw new TypeError("outline points require finite x and y coordinates");
      }
      flat.push(x, y);
    });
    return flat;
  }
  fx.outline = function outline(points, x, y, scale, width, color, options) {
    if (arguments.length < 6 || arguments.length > 7 || !Number.isFinite(x) ||
        !Number.isFinite(y) || !Number.isFinite(scale) || scale <= 0 ||
        !Number.isFinite(width) || width <= 0) {
      throw new TypeError("outline(points,x,y,scale,width,color[,options])");
    }
    const closed = options === true || Boolean(options && options.closed);
    return element(fx._outline(normalizePath(points), x, y, scale, width, color, closed),
                   2, { x, y, scale, kind: "outline" });
  };
  fx.polygon = function polygon(points, x, y, scale, color) {
    if (arguments.length !== 5 || !Number.isFinite(x) || !Number.isFinite(y) ||
        !Number.isFinite(scale) || scale <= 0) {
      throw new TypeError("polygon(points,x,y,scale,color)");
    }
    const path = normalizePath(points);
    if (path.length < 6) throw new TypeError("polygon requires at least 3 points");
    return element(fx._polygon(path, x, y, scale, color),
                   2, { x, y, scale, kind: "outline" });
  };
  fx.gradientRect = function gradientRect(x, y, width, height, top, bottom) {
    return make2d(fx._gradientRect, arguments, { x, y });
  };
  fx.background = function background(top, bottom) {
    return make2d(fx._background, arguments, {});
  };
  fx.qr = function qr(value, left, top, size, foreground, background) {
    if (typeof value !== "string" || !value.length ||
        !Number.isFinite(left) || !Number.isFinite(top) ||
        !Number.isFinite(size) || size <= 0) {
      throw new TypeError("qr(value,x,y,size[,foreground,background])");
    }
    const rows = fx._qrMatrix(value).trim().split("\n");
    if (!rows.length || rows.some(row => row.length !== rows.length)) {
      throw new Error("QR encoder returned an invalid matrix");
    }
    const dark = foreground === undefined ? 0x000000ff : foreground;
    const light = background === undefined ? 0xffffffff : background;
    const quiet = 4;
    const moduleSize = size / (rows.length + quiet * 2);
    const result = retainedGroup();
    result.add(fx.rect(left + size * 0.5, top + size * 0.5, size, size, light));
    rows.forEach((row, y) => {
      let start = -1;
      for (let x = 0; x <= row.length; x++) {
        const black = x < row.length && row[x] === "1";
        if (black && start < 0) start = x;
        if (!black && start >= 0) {
          const width = x - start;
          result.add(fx.rect(left + (quiet + start + width * 0.5) * moduleSize,
                             top + (quiet + y + 0.5) * moduleSize,
                             width * moduleSize, moduleSize, dark));
          start = -1;
        }
      }
    });
    return result;
  };
  fx.circle = function circle(x, y, radius, color) {
    return make2d(fx._circle, arguments, { x, y });
  };
  fx.sdfCircle = function sdfCircle(x, y, radius, color) {
    return make2d(fx._sdfCircle, arguments, { x, y, kind: "sdf" });
  };
  fx.sdfRoundedRect = function sdfRoundedRect(x, y, width, height, radius, color) {
    return make2d(fx._sdfRoundedRect, arguments, { x, y, kind: "sdf" });
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
  fx.backgroundImage = function backgroundImage(path, tint) {
    if (arguments.length !== 2) {
      throw new TypeError("backgroundImage(path, tint) requires exactly 2 arguments");
    }
    return make2d(fx._backgroundImage, arguments,
                  { x: fx.width * 0.5, y: fx.height * 0.5, scale: 1, kind: "image" });
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
  fx.textAntialias = function textAntialias(target, enabled) {
    return fx._textAntialias(numericHandle(target), Boolean(enabled));
  };
  fx.sdfGeometry = function sdfGeometry(target, kind, width, height, radius) {
    return fx._sdfGeometry(numericHandle(target), String(kind), Number(width),
                           Number(height), radius === undefined ? 0 : Number(radius));
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
  fx.shader = function shader(target, vertex, fragment) {
    if (fragment === undefined) return fx._shader(numericHandle(target), String(vertex));
    return fx._shader(numericHandle(target), String(vertex), String(fragment));
  };

  fx.rgba = function rgba(red, green, blue, alpha) {
    const byte = value => Math.max(0, Math.min(255, Math.round(value)));
    return (((byte(red) << 24) | (byte(green) << 16) |
             (byte(blue) << 8) | byte(alpha === undefined ? 255 : alpha)) >>> 0);
  };

  // feed() is an experimental snapshot/file helper, not the networking API.
  // Direct HTTP, WebSocket, TCP, and UDP access lives under fetch() and fx.net.
  fx.feed = function feed(path, fallback) {
    const key = String(path);
    if (feedCache.has(key)) return feedCache.get(key);
    const value = arguments.length > 1 ? fx.data(key, fallback) : fx.data(key);
    feedCache.set(key, value);
    return value;
  };

  if (typeof fx._netFetch === "function") {
    const OPEN = 0, DATA = 1, CLOSE = 2, ERROR = 3, CONNECTION = 4;

    function bytes(value) {
      if (typeof value === "string") return encode(value);
      if (value instanceof ArrayBuffer) return value;
      if (ArrayBuffer.isView(value)) {
        return value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength);
      }
      throw new TypeError("network data must be a string, ArrayBuffer, or typed array");
    }

    function encode(value) {
      const points = Array.from(String(value));
      const output = [];
      points.forEach(character => {
        const code = character.codePointAt(0);
        if (code < 0x80) output.push(code);
        else if (code < 0x800) output.push(0xc0 | code >> 6, 0x80 | code & 63);
        else if (code < 0x10000) output.push(0xe0 | code >> 12, 0x80 | code >> 6 & 63, 0x80 | code & 63);
        else output.push(0xf0 | code >> 18, 0x80 | code >> 12 & 63, 0x80 | code >> 6 & 63, 0x80 | code & 63);
      });
      return new Uint8Array(output).buffer;
    }

    function decode(value) {
      const input = value instanceof Uint8Array ? value : new Uint8Array(value);
      let result = "";
      for (let index = 0; index < input.length;) {
        const first = input[index++];
        let code = first, extra = 0;
        if ((first & 0xe0) === 0xc0) { code = first & 31; extra = 1; }
        else if ((first & 0xf0) === 0xe0) { code = first & 15; extra = 2; }
        else if ((first & 0xf8) === 0xf0) { code = first & 7; extra = 3; }
        for (let offset = 0; offset < extra; offset++) {
          if (index >= input.length || (input[index] & 0xc0) !== 0x80) {
            code = 0xfffd; break;
          }
          code = code << 6 | input[index++] & 63;
        }
        result += String.fromCodePoint(code);
      }
      return result;
    }

    class MicroFxResponse {
      constructor(raw) {
        this.status = raw.status;
        this.url = raw.url;
        this.ok = raw.status >= 200 && raw.status < 300;
        Object.defineProperty(this, "_body", { value: raw.body });
        Object.defineProperty(this, "_bodyBytes", { value: raw.bodyBytes });
      }
      text() { return Promise.resolve(this._body); }
      json() { return Promise.resolve().then(() => JSON.parse(this._body)); }
      arrayBuffer() {
        return Promise.resolve(this._bodyBytes.slice(0));
      }
    }

    function fetch(input, options) {
      const method = String(options && options.method || "GET").toUpperCase();
      if (method !== "GET") {
        return Promise.reject(new TypeError("microFX fetch currently supports GET only"));
      }
      const sourceHeaders = options && options.headers;
      const entries = !sourceHeaders ? [] : Array.isArray(sourceHeaders) ?
        sourceHeaders : Object.entries(sourceHeaders);
      if (entries.length > 32) {
        return Promise.reject(new RangeError("microFX fetch supports at most 32 headers"));
      }
      const headerLines = [];
      for (const entry of entries) {
        if (!Array.isArray(entry) || entry.length !== 2) {
          return Promise.reject(new TypeError("fetch headers must contain name/value pairs"));
        }
        const name = String(entry[0]);
        const value = String(entry[1]);
        if (!/^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/.test(name) || /[\r\n]/.test(value)) {
          return Promise.reject(new TypeError("fetch header name or value is invalid"));
        }
        headerLines.push(`${name}: ${value}`);
      }
      return Promise.resolve()
        .then(() => fx._netFetch(String(input), headerLines.join("\n")))
        .then(raw => new MicroFxResponse(raw));
    }

    function on(handle, event, callback) {
      if (typeof callback !== "function") throw new TypeError("network callback must be a function");
      fx._netOn(handle, event, callback);
    }

    function tcp(handle) {
      const socket = {
        send(value) { return fx._netSend(handle, bytes(value)); },
        close() { fx._netClose(handle); },
        onConnect(callback) { on(handle, OPEN, callback); return socket; },
        onData(callback) { on(handle, DATA, buffer => callback(new Uint8Array(buffer))); return socket; },
        onClose(callback) { on(handle, CLOSE, callback); return socket; },
        onError(callback) { on(handle, ERROR, callback); return socket; }
      };
      return socket;
    }

    function udp(options) {
      const settings = options || {};
      const handle = fx._netUdpOpen(String(settings.host || "0.0.0.0"), Number(settings.port || 0));
      const socket = {
        send(value, host, port) { return fx._netSend(handle, bytes(value), String(host), Number(port)); },
        close() { fx._netClose(handle); },
        onMessage(callback) {
          on(handle, DATA, (buffer, peer) => callback(new Uint8Array(buffer), peer));
          return socket;
        },
        onError(callback) { on(handle, ERROR, callback); return socket; }
      };
      if (settings.onMessage) socket.onMessage(settings.onMessage);
      if (settings.onError) socket.onError(settings.onError);
      return socket;
    }

    function connect(options) {
      if (!options || !options.host || !options.port) throw new TypeError("tcp.connect requires host and port");
      const socket = tcp(fx._netTcpConnect(String(options.host), Number(options.port)));
      if (options.onConnect) socket.onConnect(options.onConnect);
      if (options.onData) socket.onData(options.onData);
      if (options.onClose) socket.onClose(options.onClose);
      if (options.onError) socket.onError(options.onError);
      return socket;
    }

    function listen(options) {
      if (!options || !options.port) throw new TypeError("tcp.listen requires a port");
      const handle = fx._netTcpListen(String(options.host || "0.0.0.0"), Number(options.port));
      const server = {
        close() { fx._netClose(handle); },
        onConnection(callback) {
          on(handle, CONNECTION, clientHandle => callback(tcp(clientHandle)));
          return server;
        },
        onError(callback) { on(handle, ERROR, callback); return server; }
      };
      if (options.onConnection) server.onConnection(options.onConnection);
      if (options.onError) server.onError(options.onError);
      return server;
    }

    function websocketConnect(options) {
      const settings = typeof options === "string" ? { url: options } : options;
      if (!settings || !settings.url) {
        throw new TypeError("websocket.connect requires a ws:// or wss:// URL");
      }
      const handle = fx._netWebSocketConnect(String(settings.url));
      const socket = {
        send(value) { return fx._netWebSocketSend(handle, bytes(value)); },
        close() { fx._netClose(handle); },
        onOpen(callback) { on(handle, OPEN, callback); return socket; },
        onMessage(callback) {
          on(handle, DATA, buffer => callback(decode(buffer), new Uint8Array(buffer)));
          return socket;
        },
        onClose(callback) { on(handle, CLOSE, callback); return socket; },
        onError(callback) { on(handle, ERROR, callback); return socket; }
      };
      if (settings.onOpen) socket.onOpen(settings.onOpen);
      if (settings.onMessage) socket.onMessage(settings.onMessage);
      if (settings.onClose) socket.onClose(settings.onClose);
      if (settings.onError) socket.onError(settings.onError);
      return socket;
    }

    function latin1(data) {
      let result = "";
      for (let index = 0; index < data.length; index += 1024) {
        result += String.fromCharCode(...data.subarray(index, index + 1024));
      }
      return result;
    }

    function statusText(status) {
      return ({ 200: "OK", 201: "Created", 204: "No Content", 400: "Bad Request",
                404: "Not Found", 500: "Internal Server Error" })[status] || "Response";
    }

    function serve(options, handler) {
      if (typeof handler !== "function") throw new TypeError("http.serve requires a request handler");
      const server = listen(options);
      server.onConnection(client => {
        let wire = "";
        client.onData(chunk => {
          wire += latin1(chunk);
          if (wire.length > 256 * 1024) { client.close(); return; }
          const boundary = wire.indexOf("\r\n\r\n");
          if (boundary < 0) return;
          const lines = wire.slice(0, boundary).split("\r\n");
          const first = lines.shift().split(" ");
          const headers = {};
          lines.forEach(line => {
            const colon = line.indexOf(":");
            if (colon > 0) headers[line.slice(0, colon).trim().toLowerCase()] = line.slice(colon + 1).trim();
          });
          const length = Number(headers["content-length"] || 0);
          if (!Number.isInteger(length) || length < 0 || length > 256 * 1024) { client.close(); return; }
          if (wire.length < boundary + 4 + length) return;
          const bodyBytes = Uint8Array.from(wire.slice(boundary + 4, boundary + 4 + length), character => character.charCodeAt(0));
          const request = { method: first[0], path: first[1], headers, body: decode(bodyBytes) };
          Promise.resolve(handler(request)).catch(error => ({ status: 500, body: String(error) })).then(value => {
            const response = value && typeof value === "object" ? value : { body: value };
            const status = Number(response.status || 200);
            const body = String(response.body === undefined ? "" : response.body);
            const responseHeaders = Object.assign({}, response.headers || {}, {
              "content-length": new Uint8Array(encode(body)).length,
              "connection": "close"
            });
            let head = `HTTP/1.1 ${status} ${statusText(status)}\r\n`;
            Object.keys(responseHeaders).forEach(name => { head += `${name}: ${responseHeaders[name]}\r\n`; });
            client.send(head + "\r\n" + body);
            client.close();
          });
        });
        client.onError(() => client.close());
      });
      return server;
    }

    globalThis.fetch = fetch;
    fx.net = Object.freeze({
      fetch,
      encode: value => new Uint8Array(encode(value)),
      decode,
      udp: Object.freeze({ open: udp }),
      tcp: Object.freeze({ connect, listen }),
      websocket: Object.freeze({ connect: websocketConnect }),
      http: Object.freeze({ serve })
    });
  }

  if (typeof fx._tileMapCreate === "function") {
    const mercatorY = latitude => {
      const bounded = Math.max(-85.05112878, Math.min(85.05112878, latitude));
      const radians = bounded * Math.PI / 180;
      return (1 - Math.log(Math.tan(radians) + 1 / Math.cos(radians)) / Math.PI) * 0.5;
    };
    const inverseMercatorY = value =>
      Math.atan(Math.sinh(Math.PI * (1 - 2 * value))) * 180 / Math.PI;
    const cacheBytes = value => {
      if (value instanceof ArrayBuffer ||
          (value && Object.prototype.toString.call(value) === "[object ArrayBuffer]")) {
        return value;
      }
      if (ArrayBuffer.isView(value)) {
        return value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength);
      }
      throw new TypeError("cache data must be an ArrayBuffer or typed array");
    };

    fx.cache = Object.freeze({
      read(namespace, key, maxAgeSeconds) {
        return fx._cacheRead(String(namespace), String(key), Number(maxAgeSeconds));
      },
      write(namespace, key, value) {
        return fx._cacheWrite(String(namespace), String(key), cacheBytes(value));
      }
    });

    fx.tileMap = function tileMap(options) {
      const settings = options || {};
      const source = settings.source || {};
      const template = String(source.url || "");
      const tileSize = Number(source.tileSize || 256);
      const sourceMinZoom = Number(source.minZoom === undefined ? 0 : source.minZoom);
      const sourceMaxZoom = Number(source.maxZoom === undefined ? 20 : source.maxZoom);
      const cacheSeconds = Number((settings.cacheDays === undefined ? 7 : settings.cacheDays) * 86400);
      const filter = settings.filter || {};
      const center = Array.isArray(settings.center) ? settings.center : [0, 0];
      if (!template.includes("{z}") || !template.includes("{x}") ||
          !template.includes("{y}")) {
        throw new TypeError("tileMap source URL requires {z}, {x}, and {y}");
      }
      if (!Number.isFinite(tileSize) || tileSize <= 0 ||
          !Number.isInteger(sourceMinZoom) || !Number.isInteger(sourceMaxZoom) ||
          sourceMinZoom < 0 || sourceMaxZoom > 20 || sourceMinZoom > sourceMaxZoom ||
          !Number.isFinite(cacheSeconds) || cacheSeconds < 604800) {
        throw new RangeError("tileMap requires positive tiles, source zooms 0..20, and at least seven cache days");
      }
      const handle = fx._tileMapCreate(
        Number(filter.grayscale === undefined ? 1 : filter.grayscale),
        Number(filter.contrast === undefined ? 1 : filter.contrast),
        Number(filter.brightness === undefined ? 1 : filter.brightness),
        Number(filter.invert || 0),
        filter.tint === undefined ? 0xffffffff : filter.tint);
      const state = {
        handle,
        longitude: Number(center[0]), latitude: Number(center[1]),
        zoom: Number(settings.zoom === undefined ? 10 : settings.zoom),
        generation: 0,
        enabled: settings.enabled === undefined ? true : Boolean(settings.enabled),
        visible: settings.visible === undefined ? true : Boolean(settings.visible),
        sceneActive: true, ready: Promise.resolve(false),
        loading: false, reloadRequested: false, nextRetry: 0
      };

      function applyVisibility() {
        fx._tileMapVisible(handle,
          state.enabled && state.visible && state.sceneActive);
      }

      function coordinates() {
        if (!Number.isFinite(state.longitude) || !Number.isFinite(state.latitude) ||
            !Number.isFinite(state.zoom) || state.zoom < 0 || state.zoom > 20) {
          throw new RangeError("tileMap center and zoom must be finite and zoom must be 0..20");
        }
        const level = Math.max(sourceMinZoom,
          Math.min(sourceMaxZoom, Math.floor(state.zoom)));
        const scale = Math.pow(2, state.zoom - level);
        const count = Math.pow(2, level);
        const world = tileSize * count;
        const centerX = (state.longitude + 180) / 360 * world;
        const centerY = mercatorY(state.latitude) * world;
        const displaySize = tileSize * scale;
        const firstX = Math.floor((centerX - fx.width / (2 * scale)) / tileSize);
        const lastX = Math.floor((centerX + fx.width / (2 * scale)) / tileSize);
        const firstY = Math.max(0, Math.floor((centerY - fx.height / (2 * scale)) / tileSize));
        const lastY = Math.min(count - 1, Math.floor((centerY + fx.height / (2 * scale)) / tileSize));
        const result = [];
        for (let y = firstY; y <= lastY; y++) for (let x = firstX; x <= lastX; x++) {
          const wrappedX = (x % count + count) % count;
          const url = template.replace("{z}", level).replace("{x}", wrappedX).replace("{y}", y);
          result.push({
            url,
            x: (x * tileSize - centerX) * scale + fx.width * 0.5,
            y: (y * tileSize - centerY) * scale + fx.height * 0.5,
            size: displaySize
          });
        }
        if (!result.length || result.length > 64) {
          throw new RangeError("tileMap viewport requires between 1 and 64 tiles");
        }
        return result;
      }

      function loadTile(descriptor, index, generation) {
        const cached = fx.cache.read("tiles", descriptor.url, cacheSeconds);
        const content = cached instanceof ArrayBuffer ? Promise.resolve(cached) :
          fetch(descriptor.url).then(response => {
            if (!response.ok) throw new Error(`tile request failed: HTTP ${response.status}`);
            return response.arrayBuffer().then(buffer => {
              fx.cache.write("tiles", descriptor.url, buffer);
              return buffer;
            });
          });
        return content.then(buffer => {
          if (generation !== state.generation) return;
          fx._tileMapTile(handle, generation, index, descriptor.x, descriptor.y,
                          descriptor.size, buffer);
        });
      }

      function beginReload() {
        if (!state.enabled) return Promise.resolve(false);
        if (state.loading) { state.reloadRequested = true; return state.ready; }
        const descriptors = coordinates();
        const generation = ++state.generation;
        fx._tileMapBegin(handle, generation, descriptors.length);
        state.loading = true;state.reloadRequested = false;
        let cursor = 0;
        function worker() {
          if (cursor >= descriptors.length) return Promise.resolve();
          const index = cursor++;
          return loadTile(descriptors[index], index, generation).then(worker);
        }
        state.ready = Promise.all([worker(), worker(), worker()])
          .then(() => {
            state.loading = false;state.nextRetry = 0;
            if (state.reloadRequested) return beginReload();
            return true;
          })
          .catch(() => {
            state.loading = false;state.nextRetry = Date.now() + 10000;
            return false;
          });
        return state.ready;
      }
      state.beginReload = beginReload;

      const object = {
        handle,
        attribution: String(source.attribution || ""),
        center(longitude, latitude) {
          state.longitude = Number(longitude);state.latitude = Number(latitude);
          beginReload();return object;
        },
        zoom(value) { state.zoom = Number(value);beginReload();return object; },
        reload() { return beginReload(); },
        ready() { return state.ready; },
        isReady() { return Boolean(fx._tileMapReady(handle)); },
        project(longitude, latitude) {
          const level = Math.floor(state.zoom);
          const scale = Math.pow(2, state.zoom - level);
          const world = tileSize * Math.pow(2, level);
          let dx = (Number(longitude) - state.longitude) / 360 * world;
          if (dx > world * 0.5) dx -= world;
          if (dx < -world * 0.5) dx += world;
          return {
            x: fx.width * 0.5 + dx * scale,
            y: fx.height * 0.5 +
              (mercatorY(Number(latitude)) - mercatorY(state.latitude)) * world * scale
          };
        },
        unproject(x, y) {
          const level = Math.floor(state.zoom);
          const scale = Math.pow(2, state.zoom - level);
          const world = tileSize * Math.pow(2, level);
          return {
            longitude: state.longitude +
              (Number(x) - fx.width * 0.5) / (world * scale) * 360,
            latitude: inverseMercatorY(mercatorY(state.latitude) +
              (Number(y) - fx.height * 0.5) / (world * scale))
          };
        },
        enabled(value) {
          const next = Boolean(value);
          if (state.enabled === next) return object;
          state.enabled = next;
          if (!next) {
            state.generation++;
            state.reloadRequested = false;
          }
          applyVisibility();
          if (next) beginReload();
          return object;
        },
        visible(value) { state.visible = Boolean(value);applyVisibility();return object; },
        _sceneActive(value) { state.sceneActive = Boolean(value);applyVisibility();return object; },
        show() { return object.visible(true); },
        hide() { return object.visible(false); }
      };
      tileMaps.add(object);activeTileMaps.push(state);applyVisibility();
      if (state.enabled) beginReload();
      return object;
    };

    const EARTH_NIGHT_DATE = "2016-01-01";
    const earthMap = function earthMap(options) {
      const settings = options || {};
      const center = Array.isArray(settings.center) ? settings.center : [0, 0];
      const zoom = Number(settings.zoom === undefined ? 8 : settings.zoom);
      const nightDate = String(settings.nightDate || EARTH_NIGHT_DATE);
      if (!/^\d{4}-\d{2}-\d{2}$/.test(nightDate)) {
        throw new TypeError("maps.earth nightDate must be YYYY-MM-DD");
      }
      const day = fx.tileMap({
        source: settings.daySource || {
          url: "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
          tileSize: 256,
          maxZoom: 20,
          attribution: "Imagery © Esri, Maxar, Earthstar Geographics"
        },
        center, zoom,
        cacheDays: settings.dayCacheDays === undefined ? 30 : settings.dayCacheDays,
        filter: settings.dayFilter || { grayscale: 0, contrast: 1, brightness: 1 }
      });
      const night = fx.tileMap({
        source: settings.nightSource || {
          url: "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/" +
            "VIIRS_Night_Lights/" +
            "default/" + nightDate + "/GoogleMapsCompatible_Level8/{z}/{y}/{x}.png",
          tileSize: 256,
          maxZoom: 8,
          attribution: "Night imagery © NASA Black Marble / VIIRS"
        },
        center, zoom,
        cacheDays: settings.nightCacheDays === undefined ? 3650 : settings.nightCacheDays,
        filter: settings.nightFilter || { grayscale: 0, contrast: 1, brightness: 1 }
      });
      const object = {
        day, night, nightDate,
        center(longitude, latitude) {
          day.center(longitude, latitude);night.center(longitude, latitude);return object;
        },
        zoom(value) { day.zoom(value);night.zoom(value);return object; },
        reload() { return Promise.all([day.reload(), night.reload()]); },
        ready() { return Promise.all([day.ready(), night.ready()]); },
        isReady() { return day.isReady() && night.isReady(); },
        project(longitude, latitude) { return day.project(longitude, latitude); },
        unproject(x, y) { return day.unproject(x, y); },
        show() { day.show();night.show();return object; },
        hide() { day.hide();night.hide();return object; }
      };
      return object;
    };
    fx.maps = Object.freeze({ earth: earthMap, EARTH_NIGHT_DATE });
  }

  if (typeof fx._hdf5Decode === "function") {
    const dataTypes = Object.freeze({
      int8: Int8Array,
      uint8: Uint8Array,
      int16: Int16Array,
      uint16: Uint16Array,
      int32: Int32Array,
      uint32: Uint32Array,
      float32: Float32Array,
      float64: Float64Array
    });
    fx.data.decode = function decodeData(value, options) {
      const settings = options || {};
      const format = String(settings.format || "").toLowerCase();
      if (format !== "hdf5") {
        throw new RangeError("data.decode currently supports format: 'hdf5'");
      }
      let bytes;
      if (value instanceof ArrayBuffer ||
          (value && Object.prototype.toString.call(value) === "[object ArrayBuffer]")) {
        bytes = value;
      } else if (ArrayBuffer.isView(value)) {
        bytes = value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength);
      } else {
        throw new TypeError("data.decode input must be an ArrayBuffer or typed array");
      }
      const dataset = String(settings.dataset || "");
      const decoded = fx._hdf5Decode(
        bytes, dataset,
        settings.start === undefined ? null : settings.start,
        settings.count === undefined ? null : settings.count,
        settings.stride === undefined ? null : settings.stride,
        settings.attributes === undefined ? null : settings.attributes);
      const Constructor = dataTypes[decoded.type];
      if (!Constructor) throw new TypeError(`unsupported decoded data type: ${decoded.type}`);
      decoded.data = new Constructor(decoded.buffer);
      return decoded;
    };
  }

  fx.texture = function texture(source, options) {
    const settings = options || {};
    let handle;
    if (tileMaps.has(source)) handle = fx._gpuTextureMap(source.handle);
    else if (typeof source === "string") handle = fx._gpuTextureAsset(source);
    else throw new TypeError("texture(source) expects a tile map or asset path");
    const state = {
      enabled: settings.enabled === undefined ? true : Boolean(settings.enabled),
      visible: settings.visible === undefined ? true : Boolean(settings.visible)
    };
    function applyVisibility() {
      fx._gpuTextureVisible(handle, state.enabled && state.visible);
    }
    const object = {
      handle,
      shader(fragmentPath) {
        fx._gpuTextureShader(handle, String(fragmentPath));return object;
      },
      secondary(value) {
        if (tileMaps.has(value)) fx._gpuTextureSecondaryMap(handle, value.handle);
        else if (typeof value === "string")
          fx._gpuTextureSecondaryAsset(handle, value);
        else throw new TypeError("texture.secondary(source) expects a tile map or asset path");
        return object;
      },
      tertiary(value) {
        if (tileMaps.has(value)) fx._gpuTextureTertiaryMap(handle, value.handle);
        else if (typeof value === "string")
          fx._gpuTextureTertiaryAsset(handle, value);
        else throw new TypeError("texture.tertiary(source) expects a tile map or asset path");
        return object;
      },
      params(values) {
        if (!Array.isArray(values) || values.length > 32 ||
            values.some(value => !Number.isFinite(Number(value)))) {
          throw new TypeError("texture.params(values) accepts up to 32 finite numbers");
        }
        fx._gpuTextureParams(handle, values.map(Number));return object;
      },
      field(width, height, rgbaBytes) {
        const columns = Number(width), rows = Number(height);
        if (!Number.isInteger(columns) || !Number.isInteger(rows) ||
            columns <= 0 || rows <= 0 || columns > 64 || rows > 64) {
          throw new RangeError("texture.field dimensions must be integers from 1 to 64");
        }
        let bytes;
        if (rgbaBytes instanceof Uint8Array) bytes = rgbaBytes;
        else if (rgbaBytes instanceof ArrayBuffer) bytes = new Uint8Array(rgbaBytes);
        else if (ArrayBuffer.isView(rgbaBytes)) {
          bytes = new Uint8Array(rgbaBytes.buffer, rgbaBytes.byteOffset,
                                 rgbaBytes.byteLength);
        } else throw new TypeError("texture.field requires RGBA bytes");
        if (bytes.byteLength !== columns * rows * 4) {
          throw new RangeError("texture.field requires exactly width * height * 4 bytes");
        }
        const buffer = bytes.byteOffset === 0 &&
          bytes.byteLength === bytes.buffer.byteLength ? bytes.buffer :
          bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
        fx._gpuTextureField(handle, columns, rows, buffer);return object;
      },
      stage(value) {
        fx._gpuTextureStage(handle, String(value));return object;
      },
      blend(value) {
        fx._gpuTextureBlend(handle, value === undefined ? true : Boolean(value));
        return object;
      },
      opacity(value) {
        const opacity = Number(value);
        if (!Number.isFinite(opacity) || opacity < 0 || opacity > 1) {
          throw new RangeError("texture.opacity(value) requires a number from 0 to 1");
        }
        fx._gpuTextureOpacity(handle, opacity);return object;
      },
      enabled(value) { state.enabled = Boolean(value);applyVisibility();return object; },
      visible(value) { state.visible = Boolean(value);applyVisibility();return object; },
      show() { return object.visible(true); },
      hide() { return object.visible(false); }
    };
    gpuTextures.add(object);applyVisibility();return object;
  };

  fx.scene = function scene(options) {
    const members = [];
    const flattened = [];
    const state = { active: false, requested: false, maps: [] };
    const object = {
      name: options && options.name ? String(options.name) : "scene",
      add(value) {
        if (tileMaps.has(value)) {
          if (tileMapOwners.has(value)) throw new Error("tile map already belongs to a scene");
          tileMapOwners.set(value, object);state.maps.push(value);members.push(value);
          value._sceneActive(false);return value;
        }
        if (!value || (!elementStates.has(value) && !groups.has(value))) {
          throw new TypeError("scene.add() expects a retained element, group, or tile map");
        }
        const additions = groups.has(value) ? groupMembers.get(value) : [value];
        additions.forEach(member => {
          if (sceneOwners.has(member)) throw new Error("retained element already belongs to a scene");
        });
        additions.forEach(member => {
          sceneOwners.set(member, object);
          const memberState = elementStates.get(member);
          memberState.sceneActive = false;
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
        memberState.sceneActive = state.active;
        fx._visible(member.handle, memberState.enabled && memberState.sceneActive);
      });
      state.maps.forEach(map => map._sceneActive(state.active));
    });
    const now = Date.now();
    activeTileMaps.forEach(state => {
      if (state.enabled && !state.loading && state.nextRetry > 0 && now >= state.nextRetry) {
        state.beginReload();
      }
    });
  };
})(fx);
