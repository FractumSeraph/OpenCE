use std::collections::HashMap;
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4};
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use axum::extract::ws::{Message, WebSocket};
use kcp::{Kcp, get_conv};
use rumqttc::{AsyncClient, Event, Incoming, MqttOptions, QoS};
use subtle::ConstantTimeEq;
use tokio::net::UdpSocket;
use tokio::sync::mpsc;
use tokio::time::{MissedTickBehavior, interval};
use tracing::{info, warn};

use crate::protocol::{
    BrowserFrame, Invite, JoinIdentity, KCP_MTU, KcpOutput, TunnelCrypto, accept_and_proof,
    browser_frame, initial_joins, parse_browser_frame, public_unicast, topic,
};

const MAX_STREAMS: usize = 4;
const MAX_WS_BYTES_PER_SECOND: u64 = 2 * 1024 * 1024;
const MAX_SESSION_BYTES: u64 = 1024 * 1024 * 1024;
const PUNCH_TIMEOUT: Duration = Duration::from_secs(30);
// Addresses a host answered from that it did not list (a NAT that gives each
// destination its own port), probed as its candidates are: at most these.
const MAXIMUM_REFLEXIVE_PROBES: usize = 4;
const PEER_TIMEOUT: Duration = Duration::from_secs(20);
const SESSION_TTL: Duration = Duration::from_secs(6 * 60 * 60);
const STREAM_LINGER: Duration = Duration::from_secs(10);
/* A browser that stops reading its WebSocket must not stall the session (its
UDP, its MQTT, its timers) for good: a send that waits this long ends it. */
const WEBSOCKET_SEND_TIMEOUT: Duration = Duration::from_secs(10);
/* KCP segments (of at most 1 KiB) waiting on one stream, sent or not: the
browser has no back-pressure, so a stream the host is not draining ends the
session rather than queueing without bound. The native build stops reading
its socket at 128 (STREAM_WINDOW, port/linux/src/p2p.c). */
const MAX_STREAM_QUEUE: usize = 512;

pub struct SessionTicket {
    pub actor_id: String,
    pub identity: JoinIdentity,
    pub invite: Invite,
    pub origin: String,
    pub peer_id: String,
}

pub struct SessionConfig {
    pub global_meter: Arc<GlobalMeter>,
    pub public_ip: Ipv4Addr,
    pub udp_port_start: u16,
    pub udp_port_end: u16,
}

pub struct GlobalMeter {
    bytes: AtomicU64,
    cap: u64,
    day: AtomicU64,
}

impl GlobalMeter {
    pub fn new(cap: u64) -> Self {
        Self {
            bytes: AtomicU64::new(0),
            cap,
            day: AtomicU64::new(unix_day()),
        }
    }

    pub fn allow(&self, amount: usize) -> bool {
        let today = unix_day();
        if self.day.load(Ordering::Acquire) != today
            && self.day.swap(today, Ordering::AcqRel) != today
        {
            self.bytes.store(0, Ordering::Release);
        }
        self.bytes
            .fetch_update(Ordering::AcqRel, Ordering::Acquire, |current| {
                current
                    .checked_add(amount as u64)
                    .filter(|next| *next <= self.cap)
            })
            .is_ok()
    }
}

fn unix_day() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
        / 86_400
}

#[derive(Default)]
struct RateMeter {
    total: u64,
    window_bytes: u64,
    window_started: Option<Instant>,
}

impl RateMeter {
    fn allow(&mut self, bytes: usize) -> bool {
        let now = Instant::now();
        if self
            .window_started
            .is_none_or(|start| now.duration_since(start) >= Duration::from_secs(1))
        {
            self.window_started = Some(now);
            self.window_bytes = 0;
        }
        let bytes = bytes as u64;
        if self.window_bytes.saturating_add(bytes) > MAX_WS_BYTES_PER_SECOND
            || self.total.saturating_add(bytes) > MAX_SESSION_BYTES
        {
            return false;
        }
        self.window_bytes += bytes;
        self.total += bytes;
        true
    }
}

struct Stream {
    closed_at: Option<Instant>,
    kcp: Kcp<KcpOutput>,
    local_closed: bool,
    output: KcpOutput,
    remote_closed: bool,
}

impl Stream {
    fn new(conversation: u32) -> Result<Self, &'static str> {
        let output = KcpOutput::default();
        let mut kcp = Kcp::new(conversation, output.clone());
        kcp.set_nodelay(true, 10, 2, true);
        kcp.set_wndsize(256, 256);
        kcp.set_mtu(KCP_MTU).map_err(|_| "KCP rejected its MTU")?;
        Ok(Self {
            closed_at: None,
            kcp,
            local_closed: false,
            output,
            remote_closed: false,
        })
    }

    fn mark_closed(&mut self) {
        self.closed_at.get_or_insert_with(Instant::now);
    }
}

async fn bind_udp(start: u16, end: u16) -> Result<UdpSocket, String> {
    if start == 0 || end < start || end - start > 4_096 {
        return Err("invalid UDP port range".into());
    }
    for port in start..=end {
        if let Ok(socket) = UdpSocket::bind(SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, port)).await {
            return Ok(socket);
        }
    }
    Err("no UDP gateway ports are available".into())
}

/* The session's MQTT connections. Each broker's task holds a clone of its
client, so the connection lives as long as the task: dropping the hub (however
the session ends, with an error, a panic or not) aborts every task, which
drops its event loop and closes the broker's TCP connection. */
struct MqttHub {
    clients: Vec<AsyncClient>,
    tasks: Vec<tokio::task::JoinHandle<()>>,
}

impl MqttHub {
    /* (try_publish: a broker that is still connecting, or reconnecting, must
    not hold the session's loop while its request queue is full) */
    fn publish(&self, topic: &str, payload: Vec<u8>) -> Result<(), String> {
        let mut accepted = false;
        for client in &self.clients {
            if client
                .try_publish(topic, QoS::AtMostOnce, false, payload.clone())
                .is_ok()
            {
                accepted = true;
            }
        }
        if accepted {
            Ok(())
        } else {
            Err("all MQTT signaling queues are unavailable".into())
        }
    }
}

impl Drop for MqttHub {
    fn drop(&mut self) {
        for task in &self.tasks {
            task.abort();
        }
    }
}

async fn mqtt(
    peer_id: &str,
    join_topic: String,
) -> Result<(MqttHub, mpsc::Receiver<Vec<u8>>), String> {
    let _ = peer_id;
    let brokers = brokers();
    let (sender, receiver) = mpsc::channel(32);
    // (built as the tasks start, so an error part way drops the hub and
    // aborts the ones already running)
    let mut hub = MqttHub {
        clients: Vec::with_capacity(brokers.len()),
        tasks: Vec::with_capacity(brokers.len()),
    };
    for (index, (host, port)) in brokers.into_iter().enumerate() {
        // a fresh 64-bit id per broker, as the native build uses: two
        // sessions with the same id would knock each other off the broker
        let mut id = [0_u8; 8];
        getrandom::fill(&mut id).map_err(|_| "random id unavailable".to_string())?;
        let mut options = MqttOptions::new(format!("hceu-w{index}-{}", hex::encode(id)), host.clone(), port);
        options.set_keep_alive(Duration::from_secs(60));
        let (client, mut event_loop) = AsyncClient::new(options, 32);
        let sender = sender.clone();
        let broker = format!("{host}:{port}");
        let resubscribe = client.clone();
        let topic = join_topic.clone();
        hub.tasks.push(tokio::spawn(async move {
            loop {
                match event_loop.poll().await {
                    // (re)subscribe on every connection: a clean session
                    // does not keep the subscription across a reconnect
                    Ok(Event::Incoming(Incoming::ConnAck(_))) => {
                        if let Err(error) = resubscribe.try_subscribe(topic.clone(), QoS::AtMostOnce) {
                            warn!(%broker, error = %error, "native MQTT subscribe failed");
                        }
                    }
                    Ok(Event::Incoming(Incoming::Publish(message))) => {
                        if message.payload.len() <= 512
                            && sender.send(message.payload.to_vec()).await.is_err()
                        {
                            break;
                        }
                    }
                    Ok(_) => {}
                    Err(error) => {
                        warn!(%broker, error = %error, "native MQTT event loop retrying");
                        tokio::time::sleep(Duration::from_millis(500)).await;
                    }
                }
            }
        }));
        hub.clients.push(client);
    }
    drop(sender);
    Ok((hub, receiver))
}

/* The MQTT brokers of internet play, as the native build's brokers.txt
(port/assets/network/brokers.txt): NATIVE_BROKERS (comma-separated host:port,
at most 5) overrides them. A host answers a joiner only through the broker its
request came through, so these must overlap the hosts' own lists. */
fn brokers() -> Vec<(String, u16)> {
    const DEFAULT: [&str; 5] = [
        "halovps.fractumseraph.net:1883",
        "opence.milenko.org:1883",
        "broker.emqx.io:1883",
        "broker.hivemq.com:1883",
        "test.mosquitto.org:1883",
    ];
    let configured = std::env::var("NATIVE_BROKERS").unwrap_or_default();
    let list: Vec<String> = if configured.trim().is_empty() {
        DEFAULT.iter().map(|value| value.to_string()).collect()
    } else {
        configured.split(',').map(|value| value.trim().to_string()).filter(|value| !value.is_empty()).collect()
    };
    list.iter()
        .filter_map(|entry| {
            let (host, port) = entry.rsplit_once(':').unwrap_or((entry.as_str(), "1883"));
            Some((host.to_string(), port.parse().ok()?))
        })
        .take(5)
        .collect()
}

fn now_millis() -> u32 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u32
}

async fn send_inner(
    socket: &UdpSocket,
    destination: SocketAddrV4,
    crypto: &mut TunnelCrypto,
    inner: &[u8],
    meter: &mut RateMeter,
    global_meter: &GlobalMeter,
) -> Result<(), String> {
    let packet = crypto.seal(inner).map_err(str::to_owned)?;
    if !meter.allow(packet.len()) || !global_meter.allow(packet.len()) {
        return Err("native session bandwidth limit exceeded".into());
    }
    socket
        .send_to(&packet, destination)
        .await
        .map_err(|error| format!("UDP send failed: {error}"))?;
    Ok(())
}

async fn flush_streams(
    streams: &mut HashMap<u32, Stream>,
    socket: &UdpSocket,
    endpoint: SocketAddrV4,
    crypto: &mut TunnelCrypto,
    meter: &mut RateMeter,
    global_meter: &GlobalMeter,
) -> Result<(), String> {
    let now = now_millis();
    let mut packets = Vec::new();
    for stream in streams.values_mut() {
        stream
            .kcp
            .update(now)
            .map_err(|error| format!("KCP update failed: {error}"))?;
        packets.extend(stream.output.drain());
    }
    for packet in packets {
        if packet.len() + 1 > 1_400 {
            return Err("KCP produced an oversized tunnel packet".into());
        }
        let mut inner = Vec::with_capacity(packet.len() + 1);
        inner.push(4);
        inner.extend_from_slice(&packet);
        send_inner(socket, endpoint, crypto, &inner, meter, global_meter).await?;
    }
    streams.retain(|_, stream| {
        let delivered = stream.local_closed && stream.remote_closed && stream.kcp.wait_snd() == 0;
        let expired = stream
            .closed_at
            .is_some_and(|closed| closed.elapsed() >= STREAM_LINGER);
        !delivered && !expired
    });
    Ok(())
}

fn browser_to_tunnel(
    frame: &[u8],
    streams: &mut HashMap<u32, Stream>,
) -> Result<Vec<Vec<u8>>, String> {
    match parse_browser_frame(frame).map_err(str::to_owned)? {
        BrowserFrame::Datagram {
            source,
            destination,
            data,
        } => {
            let mut inner = Vec::with_capacity(data.len() + 5);
            inner.push(3);
            inner.extend_from_slice(&source);
            inner.extend_from_slice(&destination);
            inner.extend_from_slice(data);
            Ok(vec![inner])
        }
        BrowserFrame::StreamOpen {
            connection,
            source,
            destination,
        } => {
            if !streams.contains_key(&connection) && streams.len() >= MAX_STREAMS {
                return Err("native stream limit reached".into());
            }
            let stream = streams
                .entry(connection)
                .or_insert(Stream::new(connection)?);
            let mut message = Vec::with_capacity(5);
            message.push(b'O');
            message.extend_from_slice(&destination);
            message.extend_from_slice(&source);
            stream
                .kcp
                .send(&message)
                .map_err(|error| format!("KCP open failed: {error}"))?;
            Ok(Vec::new())
        }
        BrowserFrame::StreamData { connection, data } => {
            let stream = streams
                .get_mut(&connection)
                .ok_or("unknown native stream")?;
            if stream.local_closed {
                return Err("native stream is already closed".into());
            }
            if stream.kcp.wait_snd() + data.len().div_ceil(1_024) > MAX_STREAM_QUEUE {
                return Err("native stream send queue is full".into());
            }
            for chunk in data.chunks(1_024) {
                let mut message = Vec::with_capacity(chunk.len() + 1);
                message.push(b'D');
                message.extend_from_slice(chunk);
                stream
                    .kcp
                    .send(&message)
                    .map_err(|error| format!("KCP data failed: {error}"))?;
            }
            Ok(Vec::new())
        }
        BrowserFrame::StreamClose { connection } => {
            if let Some(stream) = streams.get_mut(&connection)
                && !stream.local_closed
            {
                stream
                    .kcp
                    .send(b"C")
                    .map_err(|error| format!("KCP close failed: {error}"))?;
                stream.local_closed = true;
                stream.mark_closed();
            }
            Ok(Vec::new())
        }
    }
}

fn tunnel_to_browser(
    inner: &[u8],
    streams: &mut HashMap<u32, Stream>,
) -> Result<Vec<Vec<u8>>, String> {
    if inner.is_empty() {
        return Err("empty native tunnel packet".into());
    }
    match inner[0] {
        3 => {
            if inner.len() < 5 || inner.len() - 5 > 1_500 {
                return Err("native datagram is malformed".into());
            }
            Ok(vec![browser_frame(
                1,
                0,
                inner[1..3].try_into().unwrap(),
                inner[3..5].try_into().unwrap(),
                &inner[5..],
            )])
        }
        4 => {
            let packet = &inner[1..];
            if packet.len() < 24 {
                return Err("native KCP packet is malformed".into());
            }
            let conversation = get_conv(packet);
            if !streams.contains_key(&conversation) && streams.len() >= MAX_STREAMS {
                return Err("native stream limit reached".into());
            }
            let stream = streams
                .entry(conversation)
                .or_insert(Stream::new(conversation)?);
            stream
                .kcp
                .input(packet)
                .map_err(|error| format!("KCP input failed: {error}"))?;
            let mut frames = Vec::new();
            while let Ok(size) = stream.kcp.peeksize() {
                if size == 0 || size > 1_025 {
                    return Err("native KCP message is malformed".into());
                }
                let mut message = vec![0_u8; size];
                stream
                    .kcp
                    .recv(&mut message)
                    .map_err(|error| format!("KCP receive failed: {error}"))?;
                match message[0] {
                    b'O' if message.len() == 5 => frames.push(browser_frame(
                        2,
                        conversation,
                        message[3..5].try_into().unwrap(),
                        message[1..3].try_into().unwrap(),
                        &[],
                    )),
                    b'D' if !stream.remote_closed => frames.push(browser_frame(
                        3,
                        conversation,
                        [0, 0],
                        [0, 0],
                        &message[1..],
                    )),
                    b'C' if message.len() == 1 => {
                        if !stream.remote_closed {
                            stream.remote_closed = true;
                            stream.mark_closed();
                            frames.push(browser_frame(4, conversation, [0, 0], [0, 0], &[]));
                        }
                    }
                    _ => return Err("native stream message is malformed".into()),
                }
            }
            Ok(frames)
        }
        1 | 2 | 5 => Ok(Vec::new()),
        _ => Err("native tunnel packet type is unknown".into()),
    }
}

/* The tunnel's ping (type 1) carries four bytes that the native host's pong
(type 2) sends back unchanged, to the address the ping came from
(tunnel_received, port/linux/src/p2p.c). Before the endpoint is known the
gateway puts a random value per candidate there instead of the clock: the
invite, and so the candidates and the tunnel's keys, are the browser's to
choose, so a sealed packet that merely claims to come from a candidate proves
nothing (its source can be forged). A pong carrying the value sent only to
that address proves that whoever answers receives packets there. */
fn probe_nonces(candidates: &[SocketAddrV4]) -> Result<HashMap<SocketAddrV4, [u8; 4]>, String> {
    let mut nonces = HashMap::with_capacity(candidates.len());
    for candidate in candidates {
        let mut nonce = [0_u8; 4];
        getrandom::fill(&mut nonce).map_err(|_| "random generator failed".to_string())?;
        nonces.insert(*candidate, nonce);
    }
    Ok(nonces)
}

/// Whether a packet (opened) from `source` is the pong to the probe sent there.
fn answers_probe(
    nonces: &HashMap<SocketAddrV4, [u8; 4]>,
    source: SocketAddrV4,
    inner: &[u8],
) -> bool {
    inner.len() >= 5
        && inner[0] == 2
        && nonces
            .get(&source)
            .is_some_and(|nonce| bool::from(nonce.ct_eq(&inner[1..5])))
}

async fn websocket_send(websocket: &mut WebSocket, message: Message) -> Result<(), String> {
    match tokio::time::timeout(WEBSOCKET_SEND_TIMEOUT, websocket.send(message)).await {
        Ok(result) => result.map_err(|error| error.to_string()),
        Err(_) => Err("browser stopped reading its WebSocket".into()),
    }
}

pub async fn run(
    mut websocket: WebSocket,
    ticket: SessionTicket,
    config: SessionConfig,
) -> Result<(), String> {
    let udp = bind_udp(config.udp_port_start, config.udp_port_end).await?;
    let port = udp.local_addr().map_err(|error| error.to_string())?.port();
    let local_candidate = SocketAddrV4::new(config.public_ip, port);
    let host_topic = topic(
        &ticket.invite.token,
        b"host",
        &ticket.invite.host_identifier,
    );
    let join_topic = topic(&ticket.invite.token, b"joiner", &ticket.identity.identifier);
    // (the hub's Drop aborts its broker tasks on every way out of here)
    let (mqtt, mut mqtt_messages) = mqtt(&ticket.peer_id, join_topic).await?;
    // (in both signalling versions until the host answers in one)
    let first_joins =
        initial_joins(&ticket.invite, &ticket.identity, local_candidate).map_err(str::to_owned)?;
    for join in &first_joins {
        mqtt.publish(&host_topic, join.clone())?;
    }

    let mut accepted = None;
    let mut proof = None;
    let mut crypto = None;
    let mut probes = HashMap::new();
    let mut endpoint = None;
    let mut reflexive_probes = 0_usize;
    let mut unlisted_packets = 0_u32;
    let mut streams = HashMap::new();
    let mut meter = RateMeter::default();
    let started = Instant::now();
    let mut last_heard = Instant::now();
    let mut last_join = Instant::now();
    let mut last_punch = Instant::now() - Duration::from_secs(1);
    let mut last_keepalive = Instant::now();
    let mut tick = interval(Duration::from_millis(10));
    tick.set_missed_tick_behavior(MissedTickBehavior::Skip);
    let mut udp_buffer = [0_u8; 2_048];
    let result: Result<(), String> = 'session: loop {
        if started.elapsed() > SESSION_TTL {
            break Err("native session expired".into());
        }
        if endpoint.is_some() && last_heard.elapsed() > PEER_TIMEOUT {
            break Err("native host stopped responding".into());
        }
        tokio::select! {
            message = websocket.recv() => {
                match message {
                    Some(Ok(Message::Binary(frame))) => {
                        if !meter.allow(frame.len()) || !config.global_meter.allow(frame.len()) {
                            break Err("native session bandwidth limit exceeded".into());
                        }
                        let Some(destination) = endpoint else { continue; };
                        let inners = browser_to_tunnel(&frame, &mut streams)?;
                        let cipher = crypto.as_mut().expect("endpoint requires crypto");
                        for inner in inners {
                            send_inner(
                                &udp,
                                destination,
                                cipher,
                                &inner,
                                &mut meter,
                                &config.global_meter,
                            ).await?;
                        }
                    }
                    Some(Ok(Message::Ping(value))) => {
                        if !meter.allow(value.len()) || !config.global_meter.allow(value.len()) {
                            break Err("native session bandwidth limit exceeded".into());
                        }
                        websocket_send(&mut websocket, Message::Pong(value)).await?;
                    }
                    Some(Ok(Message::Close(_))) | None => break Ok(()),
                    Some(Ok(Message::Text(value))) => {
                        if !meter.allow(value.len()) || !config.global_meter.allow(value.len()) {
                            break Err("native session bandwidth limit exceeded".into());
                        }
                    }
                    Some(Ok(Message::Pong(_))) => {}
                    Some(Err(error)) => break Err(format!("WebSocket failed: {error}")),
                }
            }
            received = udp.recv_from(&mut udp_buffer) => {
                let (size, source) = received.map_err(|error| format!("UDP receive failed: {error}"))?;
                let source = match source {
                    SocketAddr::V4(value) => value,
                    SocketAddr::V6(_) => continue,
                };
                let Some(cipher) = crypto.as_mut() else { continue; };
                let safe_source = endpoint.map_or_else(
                    || probes.contains_key(&source),
                    |current| current == source,
                );
                if !safe_source {
                    // (before the endpoint is known: a sealed packet from an
                    // address the host did not list is its NAT's mapping for
                    // this gateway, as ICE's peer-reflexive candidates are. It
                    // is probed with a value of its own, and only its echo
                    // makes it the endpoint: a forged source gets nothing but
                    // the few probes, as a listed candidate does)
                    if endpoint.is_none() {
                        unlisted_packets = unlisted_packets.saturating_add(1);
                        if reflexive_probes < MAXIMUM_REFLEXIVE_PROBES
                            && public_unicast(*source.ip())
                            && cipher.open(&udp_buffer[..size]).is_ok()
                        {
                            let mut nonce = [0_u8; 4];
                            getrandom::fill(&mut nonce).map_err(|_| "random generator failed".to_string())?;
                            probes.insert(source, nonce);
                            reflexive_probes += 1;
                        }
                    }
                    continue;
                }
                let Ok(mut inner) = cipher.open(&udp_buffer[..size]) else { continue; };
                if !meter.allow(size) || !config.global_meter.allow(size) {
                    break 'session Err("native session bandwidth limit exceeded".into());
                }
                if endpoint.is_none() {
                    // (only the pong to the probe sent to that very address
                    // makes it the endpoint; anything else is dropped, and the
                    // host's own pings are not answered before then)
                    if !answers_probe(&probes, source, &inner) { continue; }
                    endpoint = Some(source);
                    last_heard = Instant::now();
                    websocket_send(&mut websocket, Message::Text(
                        serde_json::json!({"type":"ready","v":1}).to_string().into(),
                    )).await?;
                    info!(actor_id = %ticket.actor_id, peer_id = %ticket.peer_id, "native tunnel connected");
                    continue;
                }
                last_heard = Instant::now();
                if inner.first() == Some(&1) && inner.len() >= 5 {
                    inner[0] = 2;
                    send_inner(
                        &udp,
                        source,
                        cipher,
                        &inner[..5],
                        &mut meter,
                        &config.global_meter,
                    ).await?;
                    continue;
                }
                for frame in tunnel_to_browser(&inner, &mut streams)? {
                    if !meter.allow(frame.len()) || !config.global_meter.allow(frame.len()) {
                        break 'session Err("native session bandwidth limit exceeded".into());
                    }
                    websocket_send(&mut websocket, Message::Binary(frame.into())).await?;
                }
            }
            message = mqtt_messages.recv() => {
                let Some(message) = message else { break Err("MQTT signaling stopped".into()); };
                if let Ok((new_accepted, new_proof)) = accept_and_proof(
                    &ticket.invite, &ticket.identity, &message, local_candidate,
                ) {
                    if accepted.is_none() {
                        crypto = Some(TunnelCrypto::new(
                            ticket.identity.identifier,
                            ticket.invite.host_identifier,
                            &new_accepted,
                        ));
                        probes = probe_nonces(&new_accepted.candidates)?;
                        accepted = Some(new_accepted);
                        proof = Some(new_proof);
                    }
                    if let Some(value) = &proof {
                        mqtt.publish(&host_topic, value.clone())?;
                        last_join = Instant::now();
                    }
                }
            }
            _ = tick.tick() => {
                let now = Instant::now();
                if endpoint.is_none() && started.elapsed() > PUNCH_TIMEOUT {
                    warn!(
                        peer_id = %ticket.peer_id,
                        answered = accepted.is_some(),
                        probed = probes.len(),
                        reflexive = reflexive_probes,
                        unlisted_packets,
                        "native host not reached"
                    );
                    break Err("native host could not be reached".into());
                }
                if endpoint.is_none() && last_join.elapsed() >= Duration::from_secs(2) {
                    match &proof {
                        Some(value) => mqtt.publish(&host_topic, value.clone())?,
                        None => {
                            for join in &first_joins {
                                mqtt.publish(&host_topic, join.clone())?;
                            }
                        }
                    }
                    last_join = now;
                }
                if endpoint.is_none() && last_punch.elapsed() >= Duration::from_millis(200) {
                    if let Some(cipher) = crypto.as_mut() {
                        // (at most four public candidates, five times a second,
                        // for at most PUNCH_TIMEOUT: as the native build punches)
                        for (destination, nonce) in &probes {
                            let mut ping = vec![1];
                            ping.extend_from_slice(nonce);
                            send_inner(
                                &udp,
                                *destination,
                                cipher,
                                &ping,
                                &mut meter,
                                &config.global_meter,
                            ).await?;
                        }
                    }
                    last_punch = now;
                }
                if let (Some(destination), Some(cipher)) = (endpoint, crypto.as_mut()) {
                    flush_streams(
                        &mut streams,
                        &udp,
                        destination,
                        cipher,
                        &mut meter,
                        &config.global_meter,
                    ).await?;
                    if last_keepalive.elapsed() >= Duration::from_secs(1) {
                        let mut ping = vec![1];
                        ping.extend_from_slice(&now_millis().to_le_bytes());
                        send_inner(
                            &udp,
                            destination,
                            cipher,
                            &ping,
                            &mut meter,
                            &config.global_meter,
                        ).await?;
                        last_keepalive = now;
                    }
                }
            }
        }
    };
    info!(
        actor_id = %ticket.actor_id,
        bytes = meter.total,
        duration_ms = started.elapsed().as_millis(),
        peer_id = %ticket.peer_id,
        "native session closed"
    );
    drop(mqtt);
    result
}

#[cfg(test)]
mod tests {
    use std::collections::HashMap;
    use std::net::{Ipv4Addr, SocketAddrV4};

    use super::{MAX_STREAM_QUEUE, answers_probe, browser_to_tunnel, probe_nonces};
    use crate::protocol::browser_frame;

    #[test]
    fn only_the_probed_address_echoing_its_nonce_is_adopted() {
        let first = SocketAddrV4::new(Ipv4Addr::new(203, 0, 114, 1), 2302);
        let second = SocketAddrV4::new(Ipv4Addr::new(198, 51, 101, 2), 2302);
        let probes = probe_nonces(&[first, second]).expect("random nonces");
        let nonce = probes[&first];
        let mut pong = vec![2];
        pong.extend_from_slice(&nonce);
        assert!(answers_probe(&probes, first, &pong));
        // (another candidate's nonce, a ping, a short packet, an address
        // that was never probed)
        assert!(!answers_probe(&probes, second, &pong) || probes[&second] == nonce);
        let mut ping = pong.clone();
        ping[0] = 1;
        assert!(!answers_probe(&probes, first, &ping));
        assert!(!answers_probe(&probes, first, &pong[..4]));
        let stranger = SocketAddrV4::new(Ipv4Addr::new(192, 0, 2, 9), 2302);
        assert!(!answers_probe(&probes, stranger, &pong));
        let mut wrong = pong.clone();
        wrong[4] ^= 1;
        assert!(!answers_probe(&probes, first, &wrong));
        assert!(!answers_probe(&HashMap::new(), first, &pong));
    }

    #[test]
    fn a_stream_the_host_does_not_drain_is_refused() {
        let mut streams = HashMap::new();
        browser_to_tunnel(&browser_frame(2, 7, [0, 1], [0, 2], &[]), &mut streams)
            .expect("stream opens");
        let data = browser_frame(3, 7, [0, 0], [0, 0], &[0x55; 16_384]);
        let mut accepted = 0;
        let error = loop {
            match browser_to_tunnel(&data, &mut streams) {
                Ok(_) => accepted += 1,
                Err(error) => break error,
            }
            assert!(accepted <= MAX_STREAM_QUEUE / 16, "queue unbounded");
        };
        assert_eq!(error, "native stream send queue is full");
        assert!(streams[&7].kcp.wait_snd() <= MAX_STREAM_QUEUE);
    }
}
