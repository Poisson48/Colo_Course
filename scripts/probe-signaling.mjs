#!/usr/bin/env node
// Probe un serveur de signaling Colo Sync.
// Usage : node scripts/probe-signaling.mjs wss://exemple/signal
const url = process.argv[2] || "wss://colo-apps.les-crevettes-cevenoles.fr/signal";
const ws = new WebSocket(url);
const timer = setTimeout(() => {
  console.error("TIMEOUT");
  process.exit(1);
}, 10000);

ws.onopen = () => {
  console.log("OPEN");
  ws.send(JSON.stringify({
    type: "register",
    protocol: 2,
    app: "colocourse",
    device_id: "probe-" + Date.now(),
  }));
  ws.send(JSON.stringify({
    type: "join",
    channel_tag: "a".repeat(32),
  }));
  setTimeout(() => ws.close(), 1500);
};
ws.onmessage = (event) => console.log("MSG", String(event.data).slice(0, 300));
ws.onerror = (event) => console.error("ERROR", event.message || event.type);
ws.onclose = (event) => {
  clearTimeout(timer);
  console.log("CLOSE", event.code);
  process.exit(0);
};
