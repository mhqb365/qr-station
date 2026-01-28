document.addEventListener("DOMContentLoaded", () => {
  const ipInput = document.getElementById("ip");
  const targetAccSelect = document.getElementById("targetAccount");
  const amtInput = document.getElementById("amt");
  const descInput = document.getElementById("desc");
  const pushBtn = document.getElementById("push");
  const searchBtn = document.getElementById("search");
  const statusDiv = document.getElementById("status");
  const espUserInput = document.getElementById("esp_user");
  const espPassInput = document.getElementById("esp_pass");

  let deviceAccounts = [];

  // Load saved settings
  chrome.storage.local.get(
    [
      "esp_ip",
      "esp_user",
      "esp_pass",
      "last_acc_idx",
      "mqtt_host",
      "mqtt_port",
      "mqtt_user",
      "mqtt_pass",
      "mqtt_enabled",
      "mqtt_status",
      "mqtt_error",
    ],
    (res) => {
      console.log("Popup loaded, MQTT status:", res.mqtt_status);
      if (res.esp_ip) {
        ipInput.value = res.esp_ip;
      }
      if (res.esp_user)
        document.getElementById("esp_user").value = res.esp_user;
      if (res.esp_pass)
        document.getElementById("esp_pass").value = res.esp_pass;

      if (res.esp_ip) {
        fetchAccounts(res.esp_ip, res.last_acc_idx);
      }
      if (res.mqtt_host)
        document.getElementById("mqtt_host").value = res.mqtt_host;
      if (res.mqtt_port)
        document.getElementById("mqtt_port").value = res.mqtt_port;
      if (res.mqtt_user)
        document.getElementById("mqtt_user").value = res.mqtt_user;
      if (res.mqtt_pass)
        document.getElementById("mqtt_pass").value = res.mqtt_pass;

      const mqttEnabled = document.getElementById("mqtt_enabled");
      mqttEnabled.checked = res.mqtt_enabled !== false;
      toggleMqttFields(mqttEnabled.checked);
      updateMqttStatusDisplay(res.mqtt_status, res.mqtt_error);
      validateMqttInputs();
      validateEspInputs();
    },
  );

  let isMqttSaving = false;

  function updateMqttStatusDisplay(status, error) {
    const indicator = document.getElementById("mqtt_status_indicator");
    const mqttInfo = document.getElementById("mqtt_info");
    if (!indicator) return;

    indicator.className = "status-dot " + (status || "disconnected");
    indicator.title = status
      ? status.charAt(0).toUpperCase() + status.slice(1)
      : "Disconnected";

    if (mqttInfo) {
      if (status === "connected" && isMqttSaving) {
        showStatus("✅ Kết nối MQTT thành công", "success", "mqtt_info");
        isMqttSaving = false;
      } else if (status === "connecting" && isMqttSaving) {
        showStatus("⏳ Đang thử kết nối...", "info", "mqtt_info");
      } else if (status === "error") {
        showStatus(
          "❌ Lỗi: " + (error || "Không thể kết nối"),
          "error",
          "mqtt_info",
        );
        isMqttSaving = false;
      } else if (status === "disconnected") {
        // Nếu là disconnected mà trước đó đang hiện lỗi thì KHÔNG ẩn lỗi đi
        if (!mqttInfo.textContent.includes("❌ Lỗi:")) {
          mqttInfo.style.display = "none";
        }
      }
    }

    if (status === "error" && error) {
      indicator.title += ": " + error;
    }
  }

  // Listen for storage changes to update status in real-time
  chrome.storage.onChanged.addListener((changes) => {
    if (changes.mqtt_status) {
      const newStatus = changes.mqtt_status.newValue;
      // Trùng với trạng thái hiện tại của UI chấm tròn thì luôn cập nhật
      updateMqttStatusDisplay(newStatus, null);
    }
    if (changes.mqtt_error) {
      chrome.storage.local.get(["mqtt_status"], (res) => {
        updateMqttStatusDisplay(res.mqtt_status, changes.mqtt_error.newValue);
      });
    }
  });

  function validateEspInputs() {
    const user = espUserInput.value.trim();
    const pass = espPassInput.value.trim();
    searchBtn.disabled = !user || !pass;
    if (searchBtn.disabled) {
      searchBtn.style.opacity = "0.5";
      searchBtn.style.cursor = "not-allowed";
    } else {
      searchBtn.style.opacity = "1";
      searchBtn.style.cursor = "pointer";
    }
  }

  [espUserInput, espPassInput].forEach((el) => {
    el.addEventListener("input", validateEspInputs);
  });

  function validateMqttInputs() {
    const host = document.getElementById("mqtt_host").value.trim();
    const port = document.getElementById("mqtt_port").value.trim();
    const enabled = document.getElementById("mqtt_enabled").checked;

    document.getElementById("mqtt_save").disabled = !enabled || !host || !port;
  }

  function toggleMqttFields(enabled) {
    document.getElementById("mqtt_fields").style.opacity = enabled
      ? "1"
      : "0.5";
    document.getElementById("mqtt_fields").style.pointerEvents = enabled
      ? "auto"
      : "none";
    validateMqttInputs();
  }

  // Hide status and validate when user starts typing new info
  ["mqtt_host", "mqtt_port", "mqtt_user", "mqtt_pass"].forEach((id) => {
    document.getElementById(id).addEventListener("input", () => {
      document.getElementById("mqtt_info").style.display = "none";
      isMqttSaving = false;
      validateMqttInputs();
    });
  });

  // MQTT Settings Listeners
  document.getElementById("mqtt_save").addEventListener("click", () => {
    const host = document.getElementById("mqtt_host").value.trim();
    const port = document.getElementById("mqtt_port").value.trim();

    if (!host || !port) return;

    isMqttSaving = true;
    showStatus("⏳ Đang kết nối...", "info", "mqtt_info");

    const settings = {
      mqtt_host: host,
      mqtt_port: port,
      mqtt_user: document.getElementById("mqtt_user").value.trim(),
      mqtt_pass: document.getElementById("mqtt_pass").value.trim(),
      mqtt_reconnect: Date.now(), // Trigger change in background.js
    };

    console.log("Saving MQTT settings:", settings);
    chrome.storage.local.set(settings);
  });

  document.getElementById("mqtt_enabled").addEventListener("change", (e) => {
    const enabled = e.target.checked;
    chrome.storage.local.set({ mqtt_enabled: enabled });
    toggleMqttFields(enabled);
  });

  async function fetchAccounts(ip, savedIdx = null) {
    try {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 3000);

      const user = document.getElementById("esp_user").value.trim();
      const pass = document.getElementById("esp_pass").value.trim();
      const headers = {};
      if (user || pass) {
        headers["Authorization"] = "Basic " + btoa(user + ":" + pass);
      }

      const res = await fetch(`http://${ip}/api/accounts`, {
        signal: controller.signal,
        headers: headers,
      });

      if (res.ok) {
        deviceAccounts = await res.json();
        renderAccountSelect(savedIdx);
        // showStatus("✅ Đã kết nối thiết bị", "success"); // Tắt bớt status để tránh đè khi đang quét
        chrome.storage.local.set({
          esp_ip: ip,
          esp_user: document.getElementById("esp_user").value.trim(),
          esp_pass: document.getElementById("esp_pass").value.trim(),
        });
        return true;
      }
    } catch (e) {
      console.log("Không thể kết nối thiết bị:", e);
    }
    return false;
  }

  function renderAccountSelect(savedIdx) {
    targetAccSelect.innerHTML = "";
    if (deviceAccounts.length === 0) {
      targetAccSelect.innerHTML =
        '<option value="">-- Thiết bị chưa có tài khoản --</option>';
      return;
    }

    deviceAccounts.forEach((acc, index) => {
      const opt = document.createElement("option");
      opt.value = index;
      opt.textContent = `${acc.on || acc.name} - ${acc.acc}`;
      targetAccSelect.appendChild(opt);
    });

    if (savedIdx !== null && deviceAccounts[savedIdx]) {
      targetAccSelect.value = savedIdx;
    } else {
      updateSavedAccount();
    }
  }

  targetAccSelect.addEventListener("change", () => {
    updateSavedAccount();
  });

  function updateSavedAccount() {
    const idx = targetAccSelect.value;
    if (idx !== "" && deviceAccounts[idx]) {
      const acc = deviceAccounts[idx];
      chrome.storage.local.set({
        last_acc_idx: idx,
        last_bin: acc.bin,
        last_acc: acc.acc,
        last_owner: acc.on || acc.name || "",
      });
    }
  }

  // --- LOGIC QUÉT THIẾT BỊ NÂNG CAO ---
  async function checkIp(ip) {
    try {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 1500);
      const user = document.getElementById("esp_user").value.trim();
      const pass = document.getElementById("esp_pass").value.trim();
      const headers = {};
      if (user || pass) {
        headers["Authorization"] = "Basic " + btoa(user + ":" + pass);
      }

      const response = await fetch(`http://${ip}/api/info`, {
        signal: controller.signal,
        headers: headers,
      });
      if (response.ok) {
        const data = await response.json();
        if (data.name === "QR Station") return true;
      }
    } catch (e) {}
    return false;
  }

  searchBtn.addEventListener("click", async () => {
    showStatus("🔍 Đang tìm thiết bị QR Station", "info");
    searchBtn.disabled = true;

    // 1. Thử host hiện tại và mDNS
    const currentIp = ipInput.value.trim();
    const initHosts = [];
    if (currentIp) initHosts.push(currentIp);
    initHosts.push("qrstation.local");

    for (const h of initHosts) {
      if (await checkIp(h)) {
        ipInput.value = h;
        await fetchAccounts(h);
        showStatus("✅ Đã tìm thấy: " + h, "success");
        searchBtn.disabled = false;
        return;
      }
    }

    // 2. Quét mạng nội bộ nâng cao
    showStatus("📡 Đang tìm trong mạng nội bộ", "info");
    const subnets = ["192.168.1", "192.168.10", "192.168.100", "192.168.0"];
    const BATCH_SIZE = 50;

    for (const subnet of subnets) {
      showStatus(`📡 Đang tìm ${subnet}.x`, "info");
      for (let i = 1; i < 255; i += BATCH_SIZE) {
        const batch = [];
        for (let j = i; j < i + BATCH_SIZE && j < 255; j++) {
          batch.push(`${subnet}.${j}`);
        }

        const results = await Promise.all(
          batch.map(async (ip) => {
            const found = await checkIp(ip);
            return found ? ip : null;
          }),
        );

        const foundIp = results.find((r) => r !== null);
        if (foundIp) {
          ipInput.value = foundIp;
          await fetchAccounts(foundIp);
          showStatus("✅ Đã tìm thấy: " + foundIp, "success");
          searchBtn.disabled = false;
          return;
        }
      }
    }

    showStatus("❌ Không tìm thấy thiết bị QR Station", "error");
    searchBtn.disabled = false;
  });

  pushBtn.addEventListener("click", async () => {
    const ip = ipInput.value.trim();
    const idx = targetAccSelect.value;
    const amt = amtInput.value.trim();
    const desc = descInput.value.trim();

    if (!ip || idx === "") {
      showStatus("Vui lòng kết nối thiết bị QR Station", "error");
      return;
    }

    const account = deviceAccounts[idx];
    showStatus("🚀 Đang tạo", "info");

    const url = `http://${ip}/api/qr?bin=${account.bin}&acc=${account.acc}&amt=${amt}&on=${encodeURIComponent(account.on || account.name)}&desc=${encodeURIComponent(desc)}`;

    const user = document.getElementById("esp_user").value.trim();
    const pass = document.getElementById("esp_pass").value.trim();
    const headers = {};
    if (user || pass) {
      headers["Authorization"] = "Basic " + btoa(user + ":" + pass);
    }

    try {
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 5000);

      const response = await fetch(url, {
        method: "GET",
        signal: controller.signal,
        headers: headers,
      });

      if (response.ok) {
        showStatus("✅ Đã tạo QR thành công", "success");
      } else {
        showStatus("❌ Lỗi: " + response.status, "error");
      }
    } catch (err) {
      showStatus("❌ Lỗi kết nối", "error");
    }
  });

  function showStatus(msg, type, targetId = "status") {
    const target =
      targetId === "status" ? statusDiv : document.getElementById(targetId);
    if (!target) return;

    target.textContent = msg;
    target.className = "status " + type;
    target.style.display = "block";

    // Success messages auto-hide after 5 seconds
    if (type === "success") {
      setTimeout(() => {
        if (target.textContent === msg) {
          target.style.display = "none";
        }
      }, 5000);
    }
  }
});
