use std::collections::VecDeque;
use std::io::{self, Write};
use std::net::{Ipv4Addr, SocketAddrV4};
use std::sync::{Arc, Mutex};

use chacha20poly1305::aead::{Aead, AeadInPlace, KeyInit};
use chacha20poly1305::{ChaCha20Poly1305, Key, Nonce, Tag};
use hmac::{Hmac, Mac};
use sha2::{Digest, Sha256};
use subtle::ConstantTimeEq;
use x25519_dalek::{PublicKey, StaticSecret};

type HmacSha256 = Hmac<Sha256>;

pub const IDENTIFIER_SIZE: usize = 6;
pub const FRAME_HEADER_SIZE: usize = 12;
pub const FRAME_MAX_SIZE: usize = 16_396;
pub const KCP_MTU: usize = 1_200;
const TUNNEL_MAGIC: u8 = 0x69;
const TUNNEL_HEADER_SIZE: usize = 1 + IDENTIFIER_SIZE + 8;
const TAG_SIZE: usize = 16;
const SIGNAL_VERSION: u8 = 3;

#[derive(Clone, Debug)]
pub struct Invite {
    pub host_hash: [u8; 16],
    pub host_identifier: [u8; IDENTIFIER_SIZE],
    pub token: [u8; 16],
}

impl Invite {
    pub fn parse(value: &str) -> Result<Self, &'static str> {
        let encoded = value
            .strip_prefix("halo://join/")
            .ok_or("invite must begin with halo://join/")?;
        if encoded.len() != 64 || !encoded.bytes().all(|byte| byte.is_ascii_hexdigit()) {
            return Err("invite must contain exactly 64 hexadecimal characters");
        }
        let bytes = hex::decode(encoded).map_err(|_| "invite is not hexadecimal")?;
        let mut host_hash = [0_u8; 16];
        let mut token = [0_u8; 16];
        host_hash.copy_from_slice(&bytes[..16]);
        token.copy_from_slice(&bytes[16..]);
        let mut host_identifier = [0_u8; IDENTIFIER_SIZE];
        host_identifier.copy_from_slice(&host_hash[..IDENTIFIER_SIZE]);
        host_identifier[0] = (host_identifier[0] & 0xfc) | 0x02;
        Ok(Self {
            host_hash,
            host_identifier,
            token,
        })
    }
}

pub fn identifier_for(public_key: &[u8; 32]) -> [u8; IDENTIFIER_SIZE] {
    let digest = Sha256::digest(public_key);
    let mut identifier = [0_u8; IDENTIFIER_SIZE];
    identifier.copy_from_slice(&digest[..IDENTIFIER_SIZE]);
    identifier[0] = (identifier[0] & 0xfc) | 0x02;
    identifier
}

fn hmac(key: &[u8], data: &[u8]) -> [u8; 32] {
    let mut mac = <HmacSha256 as Mac>::new_from_slice(key).expect("HMAC accepts every key size");
    mac.update(data);
    mac.finalize().into_bytes().into()
}

pub fn topic(token: &[u8; 16], label: &[u8], identifier: &[u8; IDENTIFIER_SIZE]) -> String {
    let mut input = Vec::with_capacity(label.len() + identifier.len());
    input.extend_from_slice(label);
    input.extend_from_slice(identifier);
    format!("hceu/3/{}", hex::encode(&hmac(token, &input)[..16]))
}

fn seal_key(token: &[u8; 16]) -> [u8; 32] {
    hmac(token, b"seal")
}

fn seal_signal(token: &[u8; 16], plaintext: &[u8]) -> Result<Vec<u8>, &'static str> {
    let mut nonce = [0_u8; 12];
    getrandom::fill(&mut nonce).map_err(|_| "random generator failed")?;
    let cipher = ChaCha20Poly1305::new(Key::from_slice(&seal_key(token)));
    let ciphertext = cipher
        .encrypt(Nonce::from_slice(&nonce), plaintext)
        .map_err(|_| "signal encryption failed")?;
    let mut sealed = Vec::with_capacity(nonce.len() + ciphertext.len());
    sealed.extend_from_slice(&nonce);
    sealed.extend_from_slice(&ciphertext);
    Ok(sealed)
}

fn open_signal(token: &[u8; 16], sealed: &[u8]) -> Result<Vec<u8>, &'static str> {
    if sealed.len() < 12 + TAG_SIZE {
        return Err("sealed signal is too short");
    }
    let cipher = ChaCha20Poly1305::new(Key::from_slice(&seal_key(token)));
    cipher
        .decrypt(Nonce::from_slice(&sealed[..12]), &sealed[12..])
        .map_err(|_| "signal authentication failed")
}

pub struct JoinIdentity {
    pub identifier: [u8; IDENTIFIER_SIZE],
    pub public_key: [u8; 32],
    pub secret_key: StaticSecret,
    pub nonce: [u8; 8],
}

impl JoinIdentity {
    pub fn generate() -> Result<Self, &'static str> {
        let mut secret = [0_u8; 32];
        let mut nonce = [0_u8; 8];
        getrandom::fill(&mut secret).map_err(|_| "random generator failed")?;
        getrandom::fill(&mut nonce).map_err(|_| "random generator failed")?;
        let secret_key = StaticSecret::from(secret);
        let public_key = PublicKey::from(&secret_key).to_bytes();
        Ok(Self {
            identifier: identifier_for(&public_key),
            public_key,
            secret_key,
            nonce,
        })
    }
}

#[derive(Clone)]
pub struct AcceptedSession {
    pub candidates: Vec<SocketAddrV4>,
    pub host_nonce: [u8; 8],
    pub receive_key: [u8; 32],
    pub send_key: [u8; 32],
}

fn pair_base(identity: &JoinIdentity, host_public: &[u8; 32]) -> Result<[u8; 32], &'static str> {
    let shared = identity
        .secret_key
        .diffie_hellman(&PublicKey::from(*host_public));
    if shared.as_bytes().ct_eq(&[0_u8; 32]).into() {
        return Err("host public key is unusable");
    }
    let mut input = Vec::with_capacity(70);
    input.extend_from_slice(b"hceu/2");
    input.extend_from_slice(&identity.public_key);
    input.extend_from_slice(host_public);
    Ok(hmac(shared.as_bytes(), &input))
}

fn message_tag(base: &[u8; 32], label: &[u8], message: &[u8]) -> [u8; TAG_SIZE] {
    let mut input = Vec::with_capacity(label.len() + message.len());
    input.extend_from_slice(label);
    input.extend_from_slice(message);
    let digest = hmac(base, &input);
    let mut tag = [0_u8; TAG_SIZE];
    tag.copy_from_slice(&digest[..TAG_SIZE]);
    tag
}

fn append_candidates(message: &mut Vec<u8>, candidates: &[SocketAddrV4]) {
    message.push(candidates.len() as u8);
    for candidate in candidates {
        message.extend_from_slice(&candidate.ip().octets());
        message.extend_from_slice(&candidate.port().to_be_bytes());
    }
}

pub fn initial_join(
    invite: &Invite,
    identity: &JoinIdentity,
    local_candidate: SocketAddrV4,
) -> Result<Vec<u8>, &'static str> {
    let mut message = Vec::with_capacity(64);
    message.extend_from_slice(&[b'J', SIGNAL_VERSION]);
    message.extend_from_slice(&identity.public_key);
    message.extend_from_slice(&identity.nonce);
    append_candidates(&mut message, &[local_candidate]);
    seal_signal(&invite.token, &message)
}

pub fn accept_and_proof(
    invite: &Invite,
    identity: &JoinIdentity,
    sealed: &[u8],
    local_candidate: SocketAddrV4,
) -> Result<(AcceptedSession, Vec<u8>), &'static str> {
    let message = open_signal(&invite.token, sealed)?;
    const FIXED: usize = 2 + 32 + 8 + 8;
    if message.len() < FIXED + 1 + TAG_SIZE || message[0] != b'A' || message[1] != SIGNAL_VERSION {
        return Err("accept signal is malformed");
    }
    let mut host_public = [0_u8; 32];
    host_public.copy_from_slice(&message[2..34]);
    if message[34..42].ct_eq(&identity.nonce).unwrap_u8() != 1
        || Sha256::digest(host_public)[..16]
            .ct_eq(&invite.host_hash)
            .unwrap_u8()
            != 1
    {
        return Err("accept signal is not from the invited host");
    }
    let count = message[FIXED] as usize;
    if count > 4 || message.len() != FIXED + 1 + count * 6 + TAG_SIZE {
        return Err("accept signal candidates are malformed");
    }
    let base = pair_base(identity, &host_public)?;
    let expected = message_tag(&base, b"accept", &message[..message.len() - TAG_SIZE]);
    if expected
        .ct_eq(&message[message.len() - TAG_SIZE..])
        .unwrap_u8()
        != 1
    {
        return Err("accept signal proof is invalid");
    }
    let mut host_nonce = [0_u8; 8];
    host_nonce.copy_from_slice(&message[42..50]);
    let mut candidates = Vec::with_capacity(count);
    for index in 0..count {
        let offset = FIXED + 1 + index * 6;
        let ip = Ipv4Addr::new(
            message[offset],
            message[offset + 1],
            message[offset + 2],
            message[offset + 3],
        );
        let port = u16::from_be_bytes([message[offset + 4], message[offset + 5]]);
        if port != 0 && public_unicast(ip) {
            candidates.push(SocketAddrV4::new(ip, port));
        }
    }
    if candidates.is_empty() {
        return Err("host offered no safe public candidate");
    }
    let mut secret_input = Vec::with_capacity(23);
    secret_input.extend_from_slice(b"session");
    secret_input.extend_from_slice(&identity.nonce);
    secret_input.extend_from_slice(&host_nonce);
    let session_secret = hmac(&base, &secret_input);
    let accepted = AcceptedSession {
        candidates,
        host_nonce,
        receive_key: hmac(&session_secret, b"host"),
        send_key: hmac(&session_secret, b"joiner"),
    };
    let mut proof = Vec::with_capacity(88);
    proof.extend_from_slice(&[b'J', SIGNAL_VERSION]);
    proof.extend_from_slice(&identity.public_key);
    proof.extend_from_slice(&identity.nonce);
    append_candidates(&mut proof, &[local_candidate]);
    proof.extend_from_slice(&accepted.host_nonce);
    let tag = message_tag(&base, b"join", &proof);
    proof.extend_from_slice(&tag);
    Ok((accepted, seal_signal(&invite.token, &proof)?))
}

pub fn public_unicast(ip: Ipv4Addr) -> bool {
    let [a, b, c, _] = ip.octets();
    !(a == 0
        || a == 10
        || a == 127
        || a >= 224
        || (a == 100 && (64..=127).contains(&b))
        || (a == 169 && b == 254)
        || (a == 172 && (16..=31).contains(&b))
        || (a == 192 && b == 0 && c == 0)
        || (a == 192 && b == 0 && c == 2)
        || (a == 192 && b == 168)
        || (a == 198 && (b == 18 || b == 19))
        || (a == 198 && b == 51 && c == 100)
        || (a == 203 && b == 0 && c == 113))
}

pub struct TunnelCrypto {
    local_identifier: [u8; IDENTIFIER_SIZE],
    remote_identifier: [u8; IDENTIFIER_SIZE],
    receive_highest: u64,
    receive_window: u64,
    send_counter: u64,
    receive_key: [u8; 32],
    send_key: [u8; 32],
}

impl TunnelCrypto {
    pub fn new(
        local_identifier: [u8; IDENTIFIER_SIZE],
        remote_identifier: [u8; IDENTIFIER_SIZE],
        accepted: &AcceptedSession,
    ) -> Self {
        Self {
            local_identifier,
            remote_identifier,
            receive_highest: 0,
            receive_window: 0,
            send_counter: 0,
            receive_key: accepted.receive_key,
            send_key: accepted.send_key,
        }
    }

    pub fn seal(&mut self, inner: &[u8]) -> Result<Vec<u8>, &'static str> {
        if inner.is_empty() || inner.len() > 1_400 {
            return Err("inner tunnel packet has an invalid size");
        }
        self.send_counter = self
            .send_counter
            .checked_add(1)
            .ok_or("packet counter exhausted")?;
        let mut packet = Vec::with_capacity(TUNNEL_HEADER_SIZE + inner.len() + TAG_SIZE);
        packet.push(TUNNEL_MAGIC);
        packet.extend_from_slice(&self.local_identifier);
        packet.extend_from_slice(&self.send_counter.to_le_bytes());
        let mut nonce = [0_u8; 12];
        nonce[4..].copy_from_slice(&self.send_counter.to_le_bytes());
        let cipher = ChaCha20Poly1305::new(Key::from_slice(&self.send_key));
        let mut body = inner.to_vec();
        let tag = cipher
            .encrypt_in_place_detached(Nonce::from_slice(&nonce), &packet, &mut body)
            .map_err(|_| "tunnel encryption failed")?;
        packet.extend_from_slice(&body);
        packet.extend_from_slice(&tag);
        Ok(packet)
    }

    pub fn open(&mut self, packet: &[u8]) -> Result<Vec<u8>, &'static str> {
        if packet.len() < TUNNEL_HEADER_SIZE + TAG_SIZE + 1
            || packet[0] != TUNNEL_MAGIC
            || packet[1..7].ct_eq(&self.remote_identifier).unwrap_u8() != 1
        {
            return Err("tunnel packet header is invalid");
        }
        let counter = u64::from_le_bytes(packet[7..15].try_into().expect("fixed counter"));
        let behind = self.receive_highest.saturating_sub(counter);
        if counter <= self.receive_highest
            && (behind >= 64 || ((self.receive_window >> behind) & 1) != 0)
        {
            return Err("tunnel packet was replayed");
        }
        let mut nonce = [0_u8; 12];
        nonce[4..].copy_from_slice(&counter.to_le_bytes());
        let body_end = packet.len() - TAG_SIZE;
        let mut body = packet[TUNNEL_HEADER_SIZE..body_end].to_vec();
        let tag = Tag::from_slice(&packet[body_end..]);
        let cipher = ChaCha20Poly1305::new(Key::from_slice(&self.receive_key));
        cipher
            .decrypt_in_place_detached(
                Nonce::from_slice(&nonce),
                &packet[..TUNNEL_HEADER_SIZE],
                &mut body,
                tag,
            )
            .map_err(|_| "tunnel packet authentication failed")?;
        if counter > self.receive_highest {
            let ahead = counter - self.receive_highest;
            self.receive_window = if ahead >= 64 {
                0
            } else {
                self.receive_window << ahead
            };
            self.receive_highest = counter;
        }
        self.receive_window |= 1_u64 << (self.receive_highest - counter);
        Ok(body)
    }
}

#[derive(Debug)]
pub enum BrowserFrame<'a> {
    Datagram {
        source: [u8; 2],
        destination: [u8; 2],
        data: &'a [u8],
    },
    StreamOpen {
        connection: u32,
        source: [u8; 2],
        destination: [u8; 2],
    },
    StreamData {
        connection: u32,
        data: &'a [u8],
    },
    StreamClose {
        connection: u32,
    },
}

pub fn parse_browser_frame(frame: &[u8]) -> Result<BrowserFrame<'_>, &'static str> {
    if frame.len() < FRAME_HEADER_SIZE
        || frame.len() > FRAME_MAX_SIZE
        || frame[0] != 0x48
        || frame[1] != 1
        || frame[3] != 0
    {
        return Err("browser frame header is invalid");
    }
    let kind = frame[2];
    let connection = u32::from_be_bytes(frame[4..8].try_into().expect("fixed connection"));
    let source = frame[8..10].try_into().expect("fixed source port");
    let destination = frame[10..12].try_into().expect("fixed destination port");
    let data = &frame[12..];
    match kind {
        1 if connection == 0
            && source != [0, 0]
            && destination != [0, 0]
            && data.len() <= 1_395 =>
        {
            Ok(BrowserFrame::Datagram {
                source,
                destination,
                data,
            })
        }
        2 if connection != 0 && source != [0, 0] && destination != [0, 0] && data.is_empty() => {
            Ok(BrowserFrame::StreamOpen {
                connection,
                source,
                destination,
            })
        }
        3 if connection != 0
            && source == [0, 0]
            && destination == [0, 0]
            && data.len() <= 16_384 =>
        {
            Ok(BrowserFrame::StreamData { connection, data })
        }
        4 if connection != 0 && source == [0, 0] && destination == [0, 0] && data.is_empty() => {
            Ok(BrowserFrame::StreamClose { connection })
        }
        _ => Err("browser frame payload is invalid"),
    }
}

pub fn browser_frame(
    kind: u8,
    connection: u32,
    source: [u8; 2],
    destination: [u8; 2],
    data: &[u8],
) -> Vec<u8> {
    let mut frame = Vec::with_capacity(FRAME_HEADER_SIZE + data.len());
    frame.extend_from_slice(&[0x48, 1, kind, 0]);
    frame.extend_from_slice(&connection.to_be_bytes());
    frame.extend_from_slice(&source);
    frame.extend_from_slice(&destination);
    frame.extend_from_slice(data);
    frame
}

#[derive(Clone, Default)]
pub struct KcpOutput(pub Arc<Mutex<VecDeque<Vec<u8>>>>);

impl KcpOutput {
    pub fn drain(&self) -> Vec<Vec<u8>> {
        self.0
            .lock()
            .expect("KCP output mutex poisoned")
            .drain(..)
            .collect()
    }
}

impl Write for KcpOutput {
    fn write(&mut self, buffer: &[u8]) -> io::Result<usize> {
        self.0
            .lock()
            .map_err(|_| io::Error::other("KCP output mutex poisoned"))?
            .push_back(buffer.to_vec());
        Ok(buffer.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_the_supplied_native_invite_shape() {
        let invite = Invite::parse(
            "halo://join/1a3899f578c05489ed38e74a70b701ccfb184685d09c1cc1a96c7287751629b6",
        )
        .unwrap();
        assert_eq!(
            hex::encode(invite.host_hash),
            "1a3899f578c05489ed38e74a70b701cc"
        );
        assert_eq!(
            hex::encode(invite.token),
            "fb184685d09c1cc1a96c7287751629b6"
        );
        assert_eq!(hex::encode(invite.host_identifier), "1a3899f578c0");
    }

    #[test]
    fn refuses_private_and_documentation_networks() {
        assert!(!public_unicast(Ipv4Addr::new(127, 0, 0, 1)));
        assert!(!public_unicast(Ipv4Addr::new(10, 0, 0, 1)));
        assert!(!public_unicast(Ipv4Addr::new(192, 0, 2, 1)));
        assert!(public_unicast(Ipv4Addr::new(1, 1, 1, 1)));
    }

    #[test]
    fn tunnel_rejects_replay() {
        let accepted = AcceptedSession {
            candidates: vec![],
            host_nonce: [0; 8],
            receive_key: [7; 32],
            send_key: [7; 32],
        };
        let mut sender = TunnelCrypto::new([2, 1, 2, 3, 4, 5], [2, 6, 7, 8, 9, 10], &accepted);
        let mut receiver = TunnelCrypto::new([2, 6, 7, 8, 9, 10], [2, 1, 2, 3, 4, 5], &accepted);
        let packet = sender.seal(b"hello").unwrap();
        assert_eq!(receiver.open(&packet).unwrap(), b"hello");
        assert!(receiver.open(&packet).is_err());
    }

    #[test]
    fn completes_the_native_v3_accept_and_proof_exchange() {
        let identity = JoinIdentity::generate().unwrap();
        let host_secret = StaticSecret::from([9_u8; 32]);
        let host_public = PublicKey::from(&host_secret).to_bytes();
        let host_digest = Sha256::digest(host_public);
        let mut host_hash = [0_u8; 16];
        host_hash.copy_from_slice(&host_digest[..16]);
        let mut host_identifier = [0_u8; IDENTIFIER_SIZE];
        host_identifier.copy_from_slice(&host_hash[..IDENTIFIER_SIZE]);
        host_identifier[0] = (host_identifier[0] & 0xfc) | 0x02;
        let invite = Invite {
            host_hash,
            host_identifier,
            token: [5_u8; 16],
        };
        let host_nonce = [7_u8; 8];
        let base = pair_base(&identity, &host_public).unwrap();
        let mut accept = vec![b'A', SIGNAL_VERSION];
        accept.extend_from_slice(&host_public);
        accept.extend_from_slice(&identity.nonce);
        accept.extend_from_slice(&host_nonce);
        append_candidates(
            &mut accept,
            &[SocketAddrV4::new(Ipv4Addr::new(1, 1, 1, 1), 2302)],
        );
        let accept_tag = message_tag(&base, b"accept", &accept);
        accept.extend_from_slice(&accept_tag);
        let sealed = seal_signal(&invite.token, &accept).unwrap();
        let (accepted, proof) = accept_and_proof(
            &invite,
            &identity,
            &sealed,
            SocketAddrV4::new(Ipv4Addr::new(8, 8, 8, 8), 40_000),
        )
        .unwrap();
        assert_eq!(accepted.host_nonce, host_nonce);
        assert_eq!(accepted.candidates[0].port(), 2302);
        let proof = open_signal(&invite.token, &proof).unwrap();
        assert_eq!(&proof[..2], &[b'J', SIGNAL_VERSION]);
        assert_eq!(&proof[2..34], &identity.public_key);
        assert_eq!(&proof[34..42], &identity.nonce);
        assert_eq!(&proof[49..57], &host_nonce);
        let expected = message_tag(&base, b"join", &proof[..proof.len() - TAG_SIZE]);
        assert_eq!(&proof[proof.len() - TAG_SIZE..], expected);
    }
}
