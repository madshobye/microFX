export const LayerKind = Object.freeze({ scene: 0, ui: 1, effect: 2 });
export const BlendMode = Object.freeze({ normal: 0, add: 1, multiply: 2, screen: 3 });
export const LayerOrigin = Object.freeze({ topLeft: 0, bottomLeft: 1 });

const MAX_LAYERS = 16;

function enumValue(table, value, label) {
  if (typeof value === "string" && Object.hasOwn(table, value)) return table[value];
  if (Object.values(table).includes(value)) return value;
  throw new RangeError(`invalid layer ${label}`);
}

function finiteRange(value, minimum, maximum, label) {
  const number = Number(value);
  if (!Number.isFinite(number) || number < minimum || number > maximum) {
    throw new RangeError(`${label} must be between ${minimum} and ${maximum}`);
  }
  return number;
}

export function createLayerStack(options = {}) {
  const maximum = options.maximum ?? MAX_LAYERS;
  if (!Number.isInteger(maximum) || maximum < 1 || maximum > MAX_LAYERS) {
    throw new RangeError(`maximum must be between 1 and ${MAX_LAYERS}`);
  }

  const layers = [];
  const names = new Set();
  const owners = new Map();

  function add(specification = {}) {
    if (layers.length >= maximum) throw new RangeError("layer stack is full");
    const id = layers.length;
    const name = specification.name ?? `layer-${id}`;
    if (typeof name !== "string" || !name.length || name.length >= 48) {
      throw new TypeError("layer name must contain 1 to 47 characters");
    }
    if (names.has(name)) throw new Error(`duplicate layer name: ${name}`);

    const logicalWidth = Number(specification.logicalWidth);
    const logicalHeight = Number(specification.logicalHeight);
    if (!Number.isInteger(logicalWidth) || logicalWidth <= 0 ||
        !Number.isInteger(logicalHeight) || logicalHeight <= 0) {
      throw new RangeError("logicalWidth and logicalHeight must be positive integers");
    }

    let kind = enumValue(LayerKind, specification.kind ?? "scene", "kind");
    let blend = enumValue(BlendMode, specification.blend ?? "normal", "blend mode");
    let opacity = finiteRange(specification.opacity ?? 1, 0, 1, "opacity");
    let pixelDensity = finiteRange(
      specification.pixelDensity ?? 1, 0.25, 1, "pixelDensity");
    let hasEffects = Boolean(specification.hasEffects ?? false);
    const origin = enumValue(
      LayerOrigin, specification.origin ?? "topLeft", "origin");
    const members = [];

    const layer = Object.freeze({
      id,
      name,
      add(member) {
        if (member === null || member === undefined) {
          throw new TypeError("layer member is required");
        }
        if (owners.has(member)) throw new Error("element already belongs to a layer");
        owners.set(member, id);
        members.push(member);
        return member;
      },
      opacity(value) {
        opacity = finiteRange(value, 0, 1, "opacity");
        return layer;
      },
      pixelDensity(value) {
        pixelDensity = finiteRange(value, 0.25, 1, "pixelDensity");
        return layer;
      },
      blend(value) {
        blend = enumValue(BlendMode, value, "blend mode");
        return layer;
      },
      effects(value) {
        hasEffects = Boolean(value);
        return layer;
      },
      descriptor() {
        return Object.freeze({
          id, name, logicalWidth, logicalHeight, origin, kind, blend,
          opacity, pixelDensity, hasEffects, memberCount: members.length
        });
      }
    });

    names.add(name);
    layers.push(layer);
    return layer;
  }

  return Object.freeze({
    add,
    all() { return Object.freeze([...layers]); },
    descriptors() { return Object.freeze(layers.map(layer => layer.descriptor())); },
    requests() {
      return Object.freeze(layers.map(layer => {
        const descriptor = layer.descriptor();
        return Object.freeze({
          kind: descriptor.kind,
          blend: descriptor.blend,
          opacity: descriptor.opacity,
          pixelDensity: descriptor.pixelDensity,
          hasEffects: descriptor.hasEffects
        });
      }));
    }
  });
}
