const express = require("express");
const mqtt = require("mqtt");
const aedes = require("aedes")();
const server = require("net").createServer(aedes.handle);

const MQTT_PORT = process.env.MQTT_PORT || 1883;
const MQTT_WS_PORT = process.env.MQTT_WS_PORT || 8883;
const MQTT_HOST = process.env.MQTT_HOST || "127.0.0.1";
const MQTT_USER = process.env.MQTT_USER || "admin";
const MQTT_PASS = process.env.MQTT_PASS || "password123";

aedes.authenticate = (client, username, password, callback) => {
  const authorized =
    username === MQTT_USER && password && password.toString() === MQTT_PASS;
  if (authorized) {
    client.user = username;
  }
  callback(null, authorized);
};

server.listen(MQTT_PORT, function () {
  console.log("MQTT broker run at port", MQTT_PORT);
});

// WS support
const httpServer = require("http").createServer();
const ws = require("websocket-stream");
ws.createServer({ server: httpServer }, aedes.handle);
httpServer.listen(MQTT_WS_PORT, function () {
  console.log("MQTT WS broker run at port", MQTT_WS_PORT);
});

aedes.on("client", function (client) {
  console.log(
    "Client connected: \x1b[33m" + (client ? client.id : client) + "\x1b[0m",
  );
});

// aedes.on("publish", async function (packet, client) {
//   if (client) {
//     console.log(
//       `Message from \x1b[33m${client.id}\x1b[0m on topic \x1b[32m${packet.topic}\x1b[0m: ${packet.payload.toString()}`,
//     );
//   }
// });

const app = express();
app.use(express.json({ limit: "1mb" }));

const PORT = process.env.PORT || 3000;
const WEBHOOK_SECRET = process.env.WEBHOOK_SECRET || "key";
const MQTT_TOPIC = "transfers";
const mqttClient = mqtt.connect(`mqtt://${MQTT_HOST}:${MQTT_PORT}`, {
  reconnectPeriod: 2000,
  username: MQTT_USER,
  password: MQTT_PASS,
});

mqttClient.on("connect", () =>
  console.log("MQTT client connected to local broker"),
);
mqttClient.on("reconnect", () => console.log("MQTT client reconnecting..."));
mqttClient.on("error", (e) => console.log("MQTT client error:", e.message));

const processed = new Set();

app.post("/", (req, res) => {
  const secret = req.headers["authorization"];
  // console.log(secret);
  if (secret !== `Bearer ${WEBHOOK_SECRET}`) {
    return res.status(401).json({ ok: false, error: "Invalid secret" });
  }

  const body = req.body || {};
  // console.log(body);
  const key = `bank:${body.transactions[0].id}`;
  if (processed.has(key)) {
    return res.status(200).json({ ok: true, duplicated: true });
  }
  processed.add(key);

  mqttClient.publish(MQTT_TOPIC, JSON.stringify(body), { qos: 1 }, (err) => {
    if (err) {
      console.log("Publish failed:", err.message);
      return res.status(500).json({ ok: false, error: "mqtt publish failed" });
    }
    console.log("Forwarded to MQTT:", body);
    return res.status(200).json({ ok: true });
  });
});

app.get("/", (req, res) => res.json({ ok: true }));

app.listen(PORT, () => console.log("QR Station server run at port", PORT));
