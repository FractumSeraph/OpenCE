use std::collections::HashMap;
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4};
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use axum::extract::ws::{Message, WebSocket};
use kcp::{Kcp, get_conv};
use rumqttc::{AsyncClient, Event, Incoming, MqttOptions, QoS};
use tokio::net::UdpSocket;
use tokio::sync::mpsc;
use tokio::time::{MissedTickBehavior, interval};
use tracing::{info, warn};

use crate::protocol::{
    BrowserFrame, Invite, JoinIdentity, KCP_MTU, KcpOutput, TunnelCrypto, accept_and_proof,
    browser_frame, initial_join, parse_browser_frame, topic,
};

const MAX_STREAMS: usize = 4;
const MAX_WS_BYTES_PER_SECOND: u64 = 2 * 1024 * 1024;
const MAX_SESSION_BYTES: u64 = 1024 * 1024 * 1024;
const PUNCH_TIMEOUT: Duration = Duration::from_secs(30);
const PEER_TIMEOUT: Duration = Duration::from_secs(20);
const SESSION_TTL: Duration = Duration::from_secs(6 * 60 * 60);
const STREAM_LINGER: Duration = Duration::from_secs(10);

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

struct MqttHub {
    clients: Vec<AsyncClient>,
    tasks: Vec<tokio::task::JoinHandle<()>>,
}

impl MqttHub {
    async fn publish(&self, topic: &str, payload: Vec<u8>) -> Result<(), String> {
        let mut accepted = false;
        for client in &self.clients {
            if client
                .publish(topic, QoS::AtMostOnce, false, payload.clone())
                .await
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

    fn abort(self) {
        for task in self.tasks {
            task.abort();
        }
    }
}

async fn mqtt(
    peer_id: &str,
    join_topic: String,
) -> Result<(MqttHub, mpsc::Receiver<Vec<u8>>), String> {
    // These are the same fixed public brokers as the native build. Neither
    // the browser nor the Worker can turn the gateway into a generic client.
    const BROKERS: [&str; 3] = ["broker.emqx.io", "broker.hivemq.com", "test.mosquitto.org"];
    let client_suffix: String = peer_id
        .bytes()
        .filter(u8::is_ascii_alphanumeric)
        .take(12)
        .map(char::from)
        .collect();
    let (sender, receiver) = mpsc::channel(32);
    let mut clients = Vec::with_capacity(BROKERS.len());
    let mut tasks = Vec::with_capacity(BROKERS.len());
    for (index, broker) in BROKERS.iter().enumerate() {
        let mut options = MqttOptions::new(format!("hceu-w{index}-{client_suffix}"), *broker, 1883);
        options.set_keep_alive(Duration::from_secs(60));
        let (client, mut event_loop) = AsyncClient::new(options, 32);
        client
            .subscribe(join_topic.clone(), QoS::AtMostOnce)
            .await
            .map_err(|error| format!("MQTT subscribe failed: {error}"))?;
        let sender = sender.clone();
        let broker = *broker;
        tasks.push(tokio::spawn(async move {
            loop {
                match event_loop.poll().await {
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
        clients.push(client);
    }
    drop(sender);
    Ok((MqttHub { clients, tasks }, receiver))
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
    let (mqtt, mut mqtt_messages) = mqtt(&ticket.peer_id, join_topic).await?;
    let first_join =
        initial_join(&ticket.invite, &ticket.identity, local_candidate).map_err(str::to_owned)?;
    mqtt.publish(&host_topic, first_join.clone()).await?;

    let mut accepted = None;
    let mut proof = None;
    let mut crypto = None;
    let mut endpoint = None;
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
    let result = 'session: loop {
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
                        websocket.send(Message::Pong(value)).await.map_err(|error| error.to_string())?;
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
                    || accepted.as_ref().is_some_and(|value: &crate::protocol::AcceptedSession| value.candidates.contains(&source)),
                    |current| current == source,
                );
                if !safe_source { continue; }
                let Ok(mut inner) = cipher.open(&udp_buffer[..size]) else { continue; };
                if !meter.allow(size) || !config.global_meter.allow(size) {
                    break 'session Err("native session bandwidth limit exceeded".into());
                }
                last_heard = Instant::now();
                if endpoint.is_none() {
                    endpoint = Some(source);
                    websocket.send(Message::Text(
                        serde_json::json!({"type":"ready","v":1}).to_string().into(),
                    )).await.map_err(|error| error.to_string())?;
                    info!(actor_id = %ticket.actor_id, peer_id = %ticket.peer_id, "native tunnel connected");
                }
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
                    websocket.send(Message::Binary(frame.into())).await.map_err(|error| error.to_string())?;
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
                        accepted = Some(new_accepted);
                        proof = Some(new_proof);
                    }
                    if let Some(value) = &proof {
                        mqtt.publish(&host_topic, value.clone()).await?;
                        last_join = Instant::now();
                    }
                }
            }
            _ = tick.tick() => {
                let now = Instant::now();
                if endpoint.is_none() && started.elapsed() > PUNCH_TIMEOUT {
                    break Err("native host could not be reached".into());
                }
                if endpoint.is_none() && last_join.elapsed() >= Duration::from_secs(2) {
                    let join = proof.as_ref().unwrap_or(&first_join);
                    mqtt.publish(&host_topic, join.clone()).await?;
                    last_join = now;
                }
                if endpoint.is_none() && last_punch.elapsed() >= Duration::from_millis(200) {
                    if let (Some(value), Some(cipher)) = (&accepted, crypto.as_mut()) {
                        for destination in &value.candidates {
                            let mut ping = vec![1];
                            ping.extend_from_slice(&now_millis().to_le_bytes());
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
    mqtt.abort();
    result
}
