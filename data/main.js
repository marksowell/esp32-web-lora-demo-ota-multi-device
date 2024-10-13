document.addEventListener('DOMContentLoaded', function() {
  const contentDiv = document.getElementById('content');
  const deviceNumberSpan = document.getElementById('deviceNumber');
  const modal = document.getElementById('modal');
  const modalMessage = document.getElementById('modal-message');
  const modalClose = document.getElementById('modal-close');

  // Load initial settings
  fetch('/ajax?action=get_settings')
    .then(response => response.json())
    .then(data => {
      deviceNumberSpan.textContent = data.deviceNumber;
    });

  // Modal Functions
  function showModal(message) {
    modalMessage.innerHTML = message;
    modal.style.display = 'block';
  }

  function closeModal() {
    modal.style.display = 'none';
  }

  modalClose.addEventListener('click', closeModal);

  window.addEventListener('click', function(event) {
    if (event.target == modal) {
      closeModal();
    }
  });

  function htmlEscape(str) {
    if (str === null || str === undefined) {
      return '';
    }
    return str.toString().replace(/&/g, '&amp;')
                         .replace(/</g, '&lt;')
                         .replace(/>/g, '&gt;')
                         .replace(/"/g, '&quot;')
                         .replace(/'/g, '&#039;');
  }

  // WebSocket Connection
  let ws;
  let wsConnected = false;
  let reconnectInterval = 1000; // Start with 1 second

  function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${window.location.host}/ws`);

    ws.onopen = function() {
      console.log('WebSocket connection established');
      wsConnected = true;
      reconnectInterval = 1000; // Reset the reconnect interval
    };

    ws.onclose = function(event) {
      console.log('WebSocket connection closed:', event);
      wsConnected = false;
      attemptReconnect();
    };

    ws.onerror = function(error) {
      console.error('WebSocket error:', error);
      wsConnected = false;
      // Close the WebSocket to trigger the onclose event
      ws.close();
    };

    ws.onmessage = function(event) {
      console.log('WebSocket message received:', event.data);
      try {
        const data = JSON.parse(event.data);
        if (data.type === 'loraMessage') {
          showModal(`Received LoRa message: ${htmlEscape(data.message)}`);
        }
      } catch (error) {
        console.error('Error in WebSocket message handler:', error);
      }
    };
  }

  function attemptReconnect() {
    if (wsConnected) return;
    console.log(`Attempting to reconnect WebSocket in ${reconnectInterval / 1000} seconds...`);
    setTimeout(() => {
      reconnectInterval = Math.min(reconnectInterval * 2, 30000); // Exponential backoff, max 30 seconds
      connectWebSocket();
    }, reconnectInterval);
  }

  // Initialize WebSocket connection
  connectWebSocket();

  function loadPage(page) {
    if (page === 'home') {
      contentDiv.innerHTML = `
        <h1>Send LoRa Messages</h1>
        <label for="customMessage">Enter a custom LoRa message to send:</label>
        <input type="text" id="customMessage" placeholder="Enter your message here">
        <button id="sendCustomMessageBtn">Send Custom LoRa Message</button>
        <p><button id="sendTestMessageBtn">Send Test LoRa Message</button></p>
      `;
      document.getElementById('sendCustomMessageBtn').addEventListener('click', sendCustomMessage);
      document.getElementById('sendTestMessageBtn').addEventListener('click', sendTestMessage);
    }
    else if (page === 'logs') {
      fetch('/ajax?action=get_logs')
        .then(response => response.json())
        .then(data => {
          let logContent = '<h1>Connection and System Logs</h1>';
          data.forEach(log => {
            logContent += `<strong>${htmlEscape(log.timestamp)} - ${htmlEscape(log.type)}</strong> - `;
            if (log.type === 'HTTP') {
              logContent += `Src IP: ${htmlEscape(log.srcIp)} - Dest IP: ${htmlEscape(log.destIp)} - `;
            }
            logContent += `${htmlEscape(log.message)}<br>`;
          });
          contentDiv.innerHTML = logContent;
        })
        .catch(error => {
          console.error('Fetch error:', error);
          showModal('Error fetching logs.');
        });
    }
    else if (page === 'status') {
      fetch('/ajax?action=get_status')
        .then(response => response.json())
        .then(data => {
          contentDiv.innerHTML = `
            <h1>System Status</h1>
            <strong>Uptime:</strong> ${data.uptime} seconds<br>
            <strong>Free Heap:</strong> ${data.freeHeap} bytes<br>
            <strong>Chip Revision:</strong> ${data.chipRevision}<br>
            <strong>WiFi Signal Strength:</strong> ${data.wifiRSSI} dBm<br>
            <strong>Current Time:</strong> ${htmlEscape(data.currentTime)}<br>
            <strong>Time Zone:</strong> ${htmlEscape(data.timeZone)}<br>
            <strong>WiFi IP:</strong> ${data.wifiIP}<br>
            <strong>Ethernet IP:</strong> ${data.ethIP}<br>
            <br><button id="rebootBtn">Reboot Device</button>
          `;
          document.getElementById('rebootBtn').addEventListener('click', rebootDevice);
        })
        .catch(error => {
          console.error('Fetch error:', error);
          showModal('Error fetching system status.');
        });
    }
    else if (page === 'settings') {
      fetch('/ajax?action=get_settings')
        .then(response => response.json())
        .then(data => {
          contentDiv.innerHTML = `
            <h1>Settings</h1>
            <label for="deviceNumber">Device Number:</label>
            <input type="number" id="deviceNumberInput" value="${data.deviceNumber}">
            <label for="siteID">Site ID:</label>
            <input type="text" id="siteIDInput" value="${htmlEscape(data.siteID)}">
            <small>Allowed characters: letters, numbers, dash (-), underscore (_)</small><br>
            <label><input type="checkbox" id="enableSystemLogs" ${data.enableSystemLogs ? 'checked' : ''}> Enable System Logs</label><br>
            <label><input type="checkbox" id="enableHttpLogs" ${data.enableHttpLogs ? 'checked' : ''}> Enable HTTP Logs</label><br>
            <label><input type="checkbox" id="enableLoRaLogs" ${data.enableLoRaLogs ? 'checked' : ''}> Enable LoRa Logs</label><br>
            <button id="saveSettingsBtn">Save Settings</button>
          `;
          document.getElementById('saveSettingsBtn').addEventListener('click', saveSettings);
        })
        .catch(error => {
          console.error('Fetch error:', error);
          showModal('Error fetching settings.');
        });
    }
  }

  function sendCustomMessage() {
    const message = document.getElementById('customMessage').value;
    fetch('/sendlora', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: `message=${encodeURIComponent(message)}`
    })
    .then(response => response.text())
    .then(text => {
      showModal(htmlEscape(text));
    })
    .catch(error => showModal('Error: ' + htmlEscape(error)));
  }

  function sendTestMessage() {
    fetch('/sendlora', {method: 'POST'})
      .then(response => response.text())
      .then(text => {
        showModal(htmlEscape(text));
      })
      .catch(error => showModal('Error: ' + htmlEscape(error)));
  }

  function rebootDevice() {
    fetch('/reboot', {method: 'POST'})
      .then(response => response.text())
      .then(text => {
        showModal(htmlEscape(text));
        // Optionally, indicate to the user that the device is rebooting
        contentDiv.innerHTML = '<h1>Device is rebooting...</h1><p>Please wait a moment and refresh the page.</p>';
      })
      .catch(error => showModal('Error: ' + htmlEscape(error)));
  }

  function saveSettings() {
    const deviceNumber = document.getElementById('deviceNumberInput').value;
    const siteID = document.getElementById('siteIDInput').value;

    // Validate deviceNumber
    if (!/^\d+$/.test(deviceNumber)) {
      showModal('Invalid Device Number. Please enter a valid number.');
      return;
    }

    // Validate siteID
    if (!/^[a-zA-Z0-9_-]+$/.test(siteID)) {
      showModal('Invalid Site ID. Allowed characters: letters, numbers, dash (-), underscore (_).');
      return;
    }

    const enableSystemLogs = document.getElementById('enableSystemLogs').checked;
    const enableHttpLogs = document.getElementById('enableHttpLogs').checked;
    const enableLoRaLogs = document.getElementById('enableLoRaLogs').checked;

    const params = new URLSearchParams();
    params.append('deviceNumber', deviceNumber);
    params.append('siteID', siteID);
    if (enableSystemLogs) params.append('enableSystemLogs', 'on');
    if (enableHttpLogs) params.append('enableHttpLogs', 'on');
    if (enableLoRaLogs) params.append('enableLoRaLogs', 'on');

    fetch('/update_settings', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: params.toString()
    })
    .then(response => response.text())
    .then(text => {
      showModal(htmlEscape(text));
    })
    .catch(error => showModal('Error: ' + htmlEscape(error)));
  }

  // Navigation links
  document.querySelectorAll('nav a').forEach(link => {
    link.addEventListener('click', function(e) {
      e.preventDefault();
      const page = this.getAttribute('data-page');
      loadPage(page);
    });
  });

  // Load the home page by default
  loadPage('home');
});
