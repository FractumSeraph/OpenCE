const BASE64_PADDING = /=+$/u;
const CROCKFORD_ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

function bytesToBase64Url(bytes: Uint8Array): string {
  let binary = "";
  for (const byte of bytes) {
    binary += String.fromCharCode(byte);
  }
  return btoa(binary)
    .replace(BASE64_PADDING, "")
    .replaceAll("+", "-")
    .replaceAll("/", "_");
}

function base64UrlToBytes(value: string): Uint8Array | null {
  try {
    const normalized = value.replaceAll("-", "+").replaceAll("_", "/");
    const padded = normalized.padEnd(Math.ceil(normalized.length / 4) * 4, "=");
    const binary = atob(padded);
    return Uint8Array.from(binary, (character) => character.charCodeAt(0));
  } catch {
    return null;
  }
}

export function randomToken(byteLength = 32): string {
  const bytes = new Uint8Array(byteLength);
  crypto.getRandomValues(bytes);
  return bytesToBase64Url(bytes);
}

export function randomRoomId(): string {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);

  const characters = Array.from(
    bytes,
    (byte) => CROCKFORD_ALPHABET[byte & 31] ?? "0",
  );

  return [
    characters.slice(0, 4).join(""),
    characters.slice(4, 8).join(""),
    characters.slice(8, 12).join(""),
    characters.slice(12, 16).join(""),
  ].join("-");
}

async function roomIdKey(secret: string): Promise<CryptoKey> {
  return crypto.subtle.importKey(
    "raw",
    new TextEncoder().encode(secret),
    { hash: "SHA-256", name: "HMAC" },
    false,
    ["sign"],
  );
}

export async function signedRoomId(secret: string): Promise<string> {
  const publicId = randomRoomId();
  const signature = await crypto.subtle.sign(
    "HMAC",
    await roomIdKey(secret),
    new TextEncoder().encode(publicId),
  );
  return `${publicId}_${bytesToBase64Url(new Uint8Array(signature))}`;
}

export async function roomIdSignatureMatches(
  roomId: string,
  secret: string,
): Promise<boolean> {
  const separator = roomId.indexOf("_");
  if (separator <= 0) return false;
  const publicId = roomId.slice(0, separator);
  const supplied = base64UrlToBytes(roomId.slice(separator + 1));
  if (supplied === null || supplied.byteLength !== 32) return false;
  const expected = await crypto.subtle.sign(
    "HMAC",
    await roomIdKey(secret),
    new TextEncoder().encode(publicId),
  );
  return crypto.subtle.timingSafeEqual(supplied, expected);
}

export function randomPeerId(role: "host" | "guest"): string {
  return `${role === "host" ? "h" : "g"}_${randomToken(12)}`;
}

export async function hashToken(token: string): Promise<ArrayBuffer> {
  return crypto.subtle.digest("SHA-256", new TextEncoder().encode(token));
}

export async function tokenMatches(
  token: string,
  expectedHash: ArrayBuffer,
): Promise<boolean> {
  const actualHash = await hashToken(token);
  return hashesMatch(actualHash, expectedHash);
}

export function hashesMatch(
  actualHash: ArrayBuffer,
  expectedHash: ArrayBuffer,
): boolean {
  return crypto.subtle.timingSafeEqual(actualHash, expectedHash);
}
