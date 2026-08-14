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
  // Direct HTTP, TCP, and UDP access lives under fetch() and fx.net.
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
      }
      text() { return Promise.resolve(this._body); }
      json() { return Promise.resolve().then(() => JSON.parse(this._body)); }
    }

    function fetch(input, options) {
      const method = String(options && options.method || "GET").toUpperCase();
      if (method !== "GET") {
        return Promise.reject(new TypeError("microFX fetch currently supports GET only"));
      }
      return fx._netFetch(String(input)).then(raw => new MicroFxResponse(raw));
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
      http: Object.freeze({ serve })
    });
  }

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
