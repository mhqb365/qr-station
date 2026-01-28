/**
 * Content script for KiotViet Sale Page
 * Inject "Đẩy QR" button next to "Thanh Toán" button
 */

(function () {
  function log(msg) {
    console.log("[QR Extension] " + msg);
  }

  log("Content script loaded on " + window.location.href);

  function parseAmount(text) {
    if (!text) return 0;
    return parseInt(text.replace(/[^0-9]/g, "")) || 0;
  }

  function formatMoney(amount) {
    return amount.toString().replace(/\B(?=(\d{3})+(?!\d))/g, ".") + "đ";
  }

  function showScreenQr(data) {
    const { bin, acc, amount, owner, desc } = data;
    const qrUrl = `https://img.vietqr.io/image/${bin}-${acc}-compact.png?amount=${amount}&addInfo=${encodeURIComponent(desc)}&accountName=${encodeURIComponent(owner)}`;

    const overlay = document.createElement("div");
    overlay.className = "qr-modal-overlay";
    overlay.id = "qr-modal-overlay";

    overlay.innerHTML = `
      <div class="qr-modal-container">
        <button class="qr-modal-close" id="qr-modal-close">&times;</button>
        
        <div class="qr-image-wrapper">
          <div class="qr-loading-spinner" id="qr-loading-spinner"></div>
          <img src="${qrUrl}" class="qr-modal-image" id="qr-modal-image" alt="VietQR">
        </div>

        <div class="qr-modal-info">
          <div class="qr-modal-amount">${formatMoney(amount)}</div>
          <div class="qr-modal-account">${acc}</div>
          <div class="qr-modal-owner">${owner}</div>
        </div>
        <p style="margin-top: 10px; font-size: 12px; color: #888;">Quét mã để thanh toán</p>
        
        <button class="qr-modal-retry-btn" id="qr-modal-push-device">
          <svg viewBox="0 0 24 24"><path d="M3 11h8V3H3v8zm2-6h4v4H5V5zM3 21h8v-8H3v8zm2-6h4v4H5v-4zM13 3v8h8V3h-8zm6 6h-4V5h4v4zM13 13h2v2h-2v-2zm2 2h2v2h-2v-2zm2-2h2v2h-2v-2zm2 2h2v2h-2v-2zm-4 4h2v2h-2v-2zm2 2h2v2h-2v-2zm-4 0h2v2h-2v-2zm2-2h2v2h-2v-2zm-2-2h2v2h-2v-2z"/></svg>
        </button>
      </div>
    `;

    document.body.appendChild(overlay);

    const closeBtn = overlay.querySelector("#qr-modal-close");
    const qrImg = overlay.querySelector("#qr-modal-image");
    const spinner = overlay.querySelector("#qr-loading-spinner");

    qrImg.onload = () => {
      spinner.style.display = "none";
      qrImg.classList.add("loaded");
    };

    closeBtn.onclick = () => {
      document.body.removeChild(overlay);
    };

    const pushDeviceBtn = overlay.querySelector("#qr-modal-push-device");
    pushDeviceBtn.onclick = async () => {
      const originalHtml = pushDeviceBtn.innerHTML;
      pushDeviceBtn.disabled = true;
      pushDeviceBtn.classList.add("loading");

      try {
        const settings = await chrome.storage.local.get(["esp_ip"]);
        if (!settings.esp_ip) {
          alert("Chưa cấu hình IP thiết bị");
          resetBtn();
          return;
        }

        const url = `http://${settings.esp_ip}/api/qr?bin=${bin}&acc=${acc}&amt=${amount}&on=${encodeURIComponent(owner)}&desc=${encodeURIComponent(desc)}`;

        chrome.runtime.sendMessage(
          { action: "push-qr", url: url },
          (response) => {
            if (response && response.success) {
              pushDeviceBtn.innerHTML = "<span>✅</span>";
              setTimeout(() => {
                resetBtn();
              }, 2000);
            } else {
              alert("Gửi thất bại. Vui lòng kiểm tra kết nối thiết bị.");
              resetBtn();
            }
          },
        );
      } catch (err) {
        resetBtn();
      }

      function resetBtn() {
        pushDeviceBtn.disabled = false;
        pushDeviceBtn.classList.remove("loading");
        pushDeviceBtn.innerHTML = originalHtml;
      }
    };

    overlay.onclick = (e) => {
      if (e.target === overlay) {
        document.body.removeChild(overlay);
      }
    };
  }

  function showMqttToast(data) {
    let container = document.getElementById("qr-toast-container");
    if (!container) {
      container = document.createElement("div");
      container.id = "qr-toast-container";
      container.className = "qr-toast-container";
      document.body.appendChild(container);
    }

    try {
      const doc = typeof data === "string" ? JSON.parse(data) : data;
      let transactions = [];

      // Extract all transactions
      if (doc.transactions && Array.isArray(doc.transactions)) {
        transactions = doc.transactions;
      } else if (doc.amount || doc.transferAmount) {
        transactions = [doc];
      } else if (Array.isArray(doc)) {
        transactions = doc;
      }

      transactions.forEach((trans, index) => {
        setTimeout(() => {
          const amount = trans.transferAmount || trans.amount || 0;
          let desc = trans.content || trans.description || trans.desc || "";
          const gateway = trans.gateway || "";
          const account = trans.accountNumber || "";

          if (amount <= 0) return;

          // Shorten description
          if (desc.length > 35) desc = desc.substring(0, 32) + "...";

          const amountStr = formatMoney(amount);
          const accountInfo =
            gateway && account
              ? ` (${gateway}: ${account})`
              : gateway || account
                ? ` (${gateway}: ${account})`
                : "";

          const toast = document.createElement("div");
          toast.className = "qr-mqtt-toast";
          toast.innerHTML = `
            <div class="qr-mqtt-toast-icon">💰</div>
            <div class="qr-mqtt-toast-content">
              <div class="qr-mqtt-toast-title">+${amountStr}${accountInfo}</div>
              <div class="qr-mqtt-toast-body">${desc || "Không có nội dung"}</div>
            </div>
          `;

          container.appendChild(toast);

          // Force reflow
          toast.offsetHeight;

          setTimeout(() => {
            toast.classList.add("show");
          }, 10);

          // Auto remove
          setTimeout(
            () => {
              toast.classList.remove("show");
              setTimeout(() => {
                if (toast.parentNode === container) {
                  container.removeChild(toast);
                }
                if (container.children.length === 0) {
                  // Keep container but it's empty
                }
              }, 500);
            },
            8000 + index * 500,
          );
        }, index * 200);
      });
    } catch (e) {
      log("Error parsing MQTT data: " + e);
    }
  }

  chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
    log("Message received from background: " + request.action);
    if (request.action === "mqtt-message") {
      log("MQTT message payload: " + request.payload);
      showMqttToast(request.payload);
    }
  });

  function findVisibleElementByText(text, selector = "*") {
    const elements = Array.from(document.querySelectorAll(selector));
    return elements.find(
      (el) =>
        el.textContent.trim().toLowerCase() === text.toLowerCase() &&
        el.offsetWidth > 0 &&
        el.offsetHeight > 0,
    );
  }

  function getSaleMode() {
    const modes = [
      { name: "quick", text: "Bán nhanh" },
      { name: "normal", text: "Bán thường" },
      { name: "delivery", text: "Bán giao hàng" },
    ];

    for (const mode of modes) {
      const el = findVisibleElementByText(mode.text);
      if (el) {
        const parentLi = el.closest("li");
        const isActive =
          el.classList.contains("active") ||
          (parentLi && parentLi.classList.contains("active")) ||
          el.closest(".active") !== null ||
          el.parentElement.classList.contains("active");

        if (isActive) return mode.name;

        const style = window.getComputedStyle(el);
        if (
          style.color === "rgb(0, 112, 192)" ||
          style.color === "rgb(0, 153, 224)" ||
          style.color === "rgb(51, 122, 183)" ||
          style.color === "rgb(0, 161, 225)"
        ) {
          return mode.name;
        }
      }
    }
    return "unknown";
  }

  function getAmount() {
    const mode = getSaleMode();
    log("Current sale mode: " + mode);

    if (mode === "normal") {
      const cartFooter = document.querySelector(".cart-footer-right");
      if (cartFooter) {
        const components = cartFooter.querySelectorAll(".form-group-inline");
        if (components.length >= 2) {
          const priceText = components[components.length - 1].innerText;
          const amount = parseAmount(priceText);
          log("Found amount in Bán thường (cart-footer): " + amount);
          return amount;
        }
      }
    }

    const targetLabel = "Khách cần trả";
    const label = findVisibleElementByText(targetLabel);

    const findValueAfterLabel = (cont, labelTxt) => {
      if (!cont) return null;
      const allText = cont.innerText || "";
      const labelIndex = allText.indexOf(labelTxt);
      if (labelIndex === -1) return null;

      const relevantText = allText.substring(labelIndex + labelTxt.length);
      const matches = relevantText.match(/[0-9][0-9\.,]*/g);

      if (matches && matches.length > 0) {
        for (const match of matches) {
          const num = parseAmount(match);
          if (num > 0 || match === "0") {
            return { innerText: match };
          }
        }
      }
      return null;
    };

    if (label) {
      let container =
        label.closest(".form-group") ||
        label.closest(".row") ||
        label.parentElement.parentElement;

      const valueEl = findValueAfterLabel(container, targetLabel);
      if (valueEl) {
        const amount = parseAmount(valueEl.innerText);
        log("Found amount in " + mode + " (Khách cần trả): " + amount);
        return amount;
      }
    }

    const paymentLabel = findVisibleElementByText("Khách thanh toán");
    if (paymentLabel) {
      const container =
        paymentLabel.closest(".form-group") ||
        paymentLabel.closest(".row") ||
        paymentLabel.parentElement.parentElement;
      const valueEl = findValueAfterLabel(container, "Khách thanh toán");
      if (valueEl) {
        return parseAmount(valueEl.innerText);
      }
    }

    return 0;
  }

  async function handlePushQr(btn) {
    const amount = getAmount();
    if (amount <= 0) {
      alert("Vui lòng nhập đơn hàng để có số tiền thanh toán > 0");
      return;
    }

    const originalHtml = btn.innerHTML;
    btn.disabled = true;
    btn.classList.add("loading");

    try {
      const settings = await chrome.storage.local.get([
        "esp_ip",
        "last_bin",
        "last_acc",
        "last_owner",
      ]);
      console.log(settings);
      const host = settings.esp_ip;
      const bin = settings.last_bin || "";
      const acc = settings.last_acc;
      const owner = settings.last_owner || "";
      const desc = "Chuyen tien";

      if (!settings.esp_ip || !settings.last_acc) {
        alert(
          "Bạn chưa cài đặt thiết bị QR Station trong extension\nVui lòng mở extension để thiết lập",
        );
        resetBtn();
        return;
      }
      const url = `http://${host}/api/qr?bin=${bin}&acc=${acc}&amt=${amount}&on=${encodeURIComponent(owner)}&desc=${encodeURIComponent(desc)}`;

      log("Pushing via background: " + url);

      // Show QR on screen immediately (parallel)
      showScreenQr({
        bin,
        acc,
        amount,
        owner,
        desc,
      });

      chrome.runtime.sendMessage(
        { action: "push-qr", url: url },
        (response) => {
          if (chrome.runtime.lastError) {
            log("Runtime error: " + chrome.runtime.lastError.message);
            alert("Lỗi kết nối extension: " + chrome.runtime.lastError.message);
            resetBtn();
            return;
          }

          if (response && response.success) {
            btn.innerHTML = "<span>✅</span>";
            btn.style.backgroundColor = "#4caf50";

            setTimeout(() => {
              resetBtn();
            }, 2000);
          } else {
            const errorMsg = response
              ? response.error || "HTTP " + response.status
              : "Không có phản hồi từ background";
            alert(
              "Lỗi từ thiết bị: " +
                errorMsg +
                "\nVui lòng kiểm tra lại thiết bị QR Station",
            );
            resetBtn();
          }
        },
      );
    } catch (err) {
      log("Error in handlePushQr: " + err);
      alert("Lỗi xử lý: " + err.message);
      resetBtn();
    }

    function resetBtn() {
      btn.disabled = false;
      btn.classList.remove("loading");
      btn.innerHTML = originalHtml;
      btn.style.backgroundColor = "";
    }
  }

  function injectButton() {
    const buttons = Array.from(
      document.querySelectorAll('button, div[role="button"], a.btn'),
    );
    const paymentBtn = buttons.find((el) => {
      const text = el.innerText.trim().toUpperCase();
      return (
        (text === "THANH TOÁN" ||
          text === "F12 THANH TOÁN" ||
          text === "HOÀN THÀNH" ||
          text === "TẠO HÓA ĐƠN") &&
        el.offsetWidth > 0 &&
        !el.closest(".modal")
      );
    });

    if (paymentBtn) {
      const amount = getAmount();
      const isDisabled = amount <= 0;

      let pushBtn = paymentBtn.parentElement.querySelector("#btnPushQrContent");

      if (!pushBtn) {
        log("Injecting Push QR button next to Payment button");

        pushBtn = document.createElement("button");
        pushBtn.id = "btnPushQrContent";
        pushBtn.className = "push-qr-button";
        pushBtn.type = "button";
        pushBtn.innerHTML = `<svg viewBox="0 0 24 24"><path d="M3 11h8V3H3v8zm2-6h4v4H5V5zM3 21h8v-8H3v8zm2-6h4v4H5v-4zM13 3v8h8V3h-8zm6 6h-4V5h4v4zM13 13h2v2h-2v-2zm2 2h2v2h-2v-2zm2-2h2v2h-2v-2zm2 2h2v2h-2v-2zm-4 4h2v2h-2v-2zm2 2h2v2h-2v-2zm-4 0h2v2h-2v-2zm2-2h2v2h-2v-2zm-2-2h2v2h-2v-2z"/></svg>`;

        pushBtn.onclick = (e) => {
          e.preventDefault();
          e.stopPropagation();
          handlePushQr(pushBtn);
        };

        paymentBtn.parentNode.insertBefore(pushBtn, paymentBtn);
      }

      pushBtn.disabled = isDisabled;
      if (isDisabled) {
        pushBtn.style.opacity = "0.4";
        pushBtn.style.cursor = "not-allowed";
        pushBtn.style.filter = "grayscale(1)";
      } else {
        pushBtn.style.opacity = "1";
        pushBtn.style.cursor = "pointer";
        pushBtn.style.filter = "none";
      }
    }
  }

  const observer = new MutationObserver((mutations) => {
    injectButton();
  });

  observer.observe(document.body, {
    childList: true,
    subtree: true,
  });

  injectButton();
})();
