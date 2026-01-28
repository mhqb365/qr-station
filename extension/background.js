// Background service worker for QR Station Link
var window = self;
var global = self;
var document = {
  createElement: function () {
    return {};
  },
  location: self.location,
};
var localStorage = {
  getItem: function () {
    return null;
  },
  setItem: function () {},
  removeItem: function () {},
};
var navigator = {
  userAgent: "Mozilla/5.0",
};

importScripts("mqtt.min.js");

let mqttClient = null;
let retryCount = 0;
const MAX_RETRIES = 5;

function connectMQTT() {
  chrome.storage.local.get(
    ["mqtt_host", "mqtt_port", "mqtt_user", "mqtt_pass", "mqtt_enabled"],
    (settings) => {
      if (mqttClient) {
        mqttClient.end(true); // Force end existing client
        mqttClient = null;
      }

      if (settings.mqtt_enabled === false) {
        console.log("MQTT is disabled in settings");
        chrome.storage.local.set({ mqtt_status: "disconnected" });
        return;
      }

      const host = settings.mqtt_host;
      const port = settings.mqtt_port;
      const user = settings.mqtt_user || "";
      const pass = settings.mqtt_pass || "";

      if (!host || !port) {
        console.log("MQTT settings incomplete, skipping connection");
        chrome.storage.local.set({ mqtt_status: "disconnected" });
        return;
      }

      const url = `ws://${host}:${port}`;
      console.log(
        `Connecting to MQTT (Attempt ${retryCount + 1}/${MAX_RETRIES}):`,
        url,
      );

      mqttClient = mqtt.connect(url, {
        username: user,
        password: pass,
        reconnectPeriod: 5000,
        keepalive: 60,
        connectTimeout: 10 * 1000,
        clientId: "qr_link_" + Math.random().toString(16).substr(2, 8),
      });

      chrome.storage.local.set({ mqtt_status: "connecting" });

      mqttClient.on("connect", () => {
        console.log("MQTT connected");
        retryCount = 0; // Reset count on success
        chrome.storage.local.set({
          mqtt_status: "connected",
          mqtt_error: null,
        });
        mqttClient.subscribe("transfers", (err) => {
          if (!err) {
            console.log("Subscribed to transfers");
          }
        });
      });

      mqttClient.on("message", (topic, message) => {
        if (topic === "transfers") {
          const payload = message.toString();
          console.log("Received MQTT message:", payload);

          chrome.storage.local.get(["mqtt_status"], (res) => {
            if (res.mqtt_status !== "connected") {
              chrome.storage.local.set({
                mqtt_status: "connected",
                mqtt_error: null,
              });
            }
          });

          // Broadcast to all tabs
          chrome.tabs.query({}, (tabs) => {
            tabs.forEach((tab) => {
              chrome.tabs
                .sendMessage(tab.id, {
                  action: "mqtt-message",
                  topic,
                  payload,
                })
                .catch(() => {});
            });
          });
        }
      });

      mqttClient.on("reconnect", () => {
        retryCount++;
        console.log(
          `MQTT reconnecting... Attempt ${retryCount}/${MAX_RETRIES}`,
        );

        if (retryCount >= MAX_RETRIES) {
          console.log("Max retries reached, giving up.");
          if (mqttClient) {
            mqttClient.end(true);
            mqttClient = null;
          }
          chrome.storage.local.set({
            mqtt_status: "error",
            mqtt_error: `Đã thử ${MAX_RETRIES} lần không thành công. Vui lòng kiểm tra lại cấu hình.`,
          });
        } else {
          chrome.storage.local.set({ mqtt_status: "connecting" });
        }
      });

      mqttClient.on("close", () => {
        console.log("MQTT closed");
        if (mqttClient && !mqttClient.connected && !mqttClient.reconnecting) {
          chrome.storage.local.set({ mqtt_status: "disconnected" });
        }
      });

      mqttClient.on("error", (err) => {
        console.error("MQTT Error:", err);
        // Error events don't necessarily mean we stop retrying,
        // but we'll report the latest one.
        chrome.storage.local.set({
          mqtt_status: "error",
          mqtt_error: err.message,
        });
      });
    },
  );
}

// Periodic status sync
setInterval(() => {
  if (mqttClient) {
    const actualStatus = mqttClient.connected
      ? "connected"
      : mqttClient.reconnecting
        ? "connecting"
        : "disconnected";
    chrome.storage.local.get(["mqtt_status"], (res) => {
      if (res.mqtt_status !== actualStatus) {
        chrome.storage.local.set({ mqtt_status: actualStatus });
      }
    });
  }
}, 5000);

// Keep Service Worker alive and check connection
chrome.alarms.create("mqtt-keepalive", { periodInMinutes: 0.5 });
chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name === "mqtt-keepalive") {
    // console.log("Heartbeat: Checking MQTT connection...");
    chrome.storage.local.get(
      ["mqtt_host", "mqtt_port", "mqtt_enabled"],
      (settings) => {
        if (
          settings.mqtt_host &&
          settings.mqtt_port &&
          settings.mqtt_enabled !== false
        ) {
          if (!mqttClient || !mqttClient.connected) {
            console.log("Heartbeat: MQTT not connected, reconnecting...");
            connectMQTT();
          }
        }
      },
    );
  }
});

// Initial connection - only if settings are complete
chrome.storage.local.get(
  ["mqtt_host", "mqtt_port", "mqtt_enabled"],
  (settings) => {
    if (
      settings.mqtt_host &&
      settings.mqtt_port &&
      settings.mqtt_enabled !== false
    ) {
      connectMQTT();
    } else {
      chrome.storage.local.set({ mqtt_status: "disconnected" });
    }
  },
);

// Reconnect when settings change
chrome.storage.onChanged.addListener((changes) => {
  if (
    changes.mqtt_host ||
    changes.mqtt_port ||
    changes.mqtt_user ||
    changes.mqtt_pass ||
    changes.mqtt_enabled ||
    changes.mqtt_reconnect
  ) {
    console.log("MQTT settings changed, reconnecting...");
    retryCount = 0; // Reset counter for new settings
    connectMQTT();
  }
});

chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
  if (request.action === "push-qr") {
    const { url } = request;

    chrome.storage.local.get(["esp_user", "esp_pass"], (res) => {
      const headers = {};
      if (res.esp_user || res.esp_pass) {
        headers["Authorization"] =
          "Basic " + btoa((res.esp_user || "") + ":" + (res.esp_pass || ""));
      }

      fetch(url, { method: "GET", headers: headers })
        .then((response) => {
          if (response.ok) {
            sendResponse({ success: true });
          } else {
            sendResponse({ success: false, status: response.status });
          }
        })
        .catch((error) => {
          sendResponse({ success: false, error: error.message });
        });
    });

    return true; // Keep message channel open for async response
  }
});
