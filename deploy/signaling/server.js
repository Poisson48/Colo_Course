/**
 * Colo Course — WebRTC Signaling Server
 *
 * Lightweight WebSocket relay for SDP/ICE exchange between peers.
 * Carries NO shopping list data — only WebRTC negotiation messages.
 *
 * Protocol (JSON over WebSocket):
 *
 *   → register { device_id }
 *   → join { channel_tag }
 *   ← peer_joined { device_id, channel_tag }     (broadcast to room)
 *   → offer { to, channel_tag, sdp }
 *   ← offer { from, channel_tag, sdp }           (forwarded to target)
 *   → answer { to, channel_tag, sdp }
 *   ← answer { from, channel_tag, sdp }           (forwarded to target)
 *   → candidate { to, channel_tag, candidate, sdpMid }
 *   ← candidate { from, channel_tag, candidate, sdpMid } (forwarded)
 *   ← peer_left { device_id, channel_tag }        (broadcast on disconnect)
 */

const { WebSocketServer } = require("ws");

const PORT = parseInt(process.env.PORT || "7778", 10);

const wss = new WebSocketServer({ port: PORT });

// State: deviceId → Set<channelTag>
const deviceChannels = new Map();
// State: channelTag → Set<deviceId>
const channelDevices = new Map();
// State: deviceId → ws
const deviceSockets = new Map();

console.log(`[signaling] listening on port ${PORT}`);

wss.on("connection", (ws) => {
  let deviceId = null;

  ws.on("message", (data) => {
    let msg;
    try {
      msg = JSON.parse(data.toString());
    } catch {
      return;
    }

    switch (msg.type) {
      case "register":
        deviceId = msg.device_id;
        if (!deviceId) return;
        // If this device was previously connected, clean up.
        cleanupDevice(deviceId);
        deviceSockets.set(deviceId, ws);
        deviceChannels.set(deviceId, new Set());
        console.log(`[signaling] registered ${deviceId}`);
        break;

      case "join":
        if (!deviceId || !msg.channel_tag) return;
        const tag = msg.channel_tag;
        // Add device to channel.
        let channels = deviceChannels.get(deviceId);
        if (!channels) {
          channels = new Set();
          deviceChannels.set(deviceId, channels);
        }
        channels.add(tag);

        let members = channelDevices.get(tag);
        if (!members) {
          members = new Set();
          channelDevices.set(tag, members);
        }
        // Notify existing members about the new peer.
        for (const memberId of members) {
          if (memberId === deviceId) continue;
          send(memberId, {
            type: "peer_joined",
            device_id: deviceId,
            channel_tag: tag,
          });
          // Also tell the new peer about existing members.
          send(deviceId, {
            type: "peer_joined",
            device_id: memberId,
            channel_tag: tag,
          });
        }
        members.add(deviceId);
        console.log(`[signaling] ${deviceId} joined ${tag} (${members.size} members)`);
        break;

      case "offer":
      case "answer":
      case "candidate":
        if (!deviceId || !msg.to) return;
        // Forward to target, adding "from" field.
        send(msg.to, { ...msg, from: deviceId, to: undefined });
        break;
    }
  });

  ws.on("close", () => {
    if (!deviceId) return;
    console.log(`[signaling] ${deviceId} disconnected`);
    // Notify all channels this device was in.
    const channels = deviceChannels.get(deviceId);
    if (channels) {
      for (const tag of channels) {
        const members = channelDevices.get(tag);
        if (members) {
          members.delete(deviceId);
          for (const memberId of members) {
            send(memberId, {
              type: "peer_left",
              device_id: deviceId,
              channel_tag: tag,
            });
          }
          if (members.size === 0) channelDevices.delete(tag);
        }
      }
    }
    deviceChannels.delete(deviceId);
    deviceSockets.delete(deviceId);
  });

  ws.on("error", (err) => {
    console.error(`[signaling] ws error:`, err.message);
  });
});

function send(deviceId, msg) {
  const ws = deviceSockets.get(deviceId);
  if (ws && ws.readyState === 1 /* OPEN */) {
    ws.send(JSON.stringify(msg));
  }
}

function cleanupDevice(deviceId) {
  const oldWs = deviceSockets.get(deviceId);
  if (oldWs) {
    try { oldWs.close(); } catch {}
  }
  const channels = deviceChannels.get(deviceId);
  if (channels) {
    for (const tag of channels) {
      const members = channelDevices.get(tag);
      if (members) members.delete(deviceId);
    }
  }
  deviceChannels.delete(deviceId);
  deviceSockets.delete(deviceId);
}