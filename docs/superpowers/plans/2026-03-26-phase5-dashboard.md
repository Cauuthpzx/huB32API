# Phase 5: Teacher Web Dashboard — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a zero-install teacher web dashboard served as static files. Teachers login via JWT, select a room, view a grid of student screens (via mediasoup-client.js WebRTC consumption), and control features (lock, message, power, open app/web). Admin panel for managing schools, locations, teachers, and viewing audit logs.

**Architecture:** Vanilla JavaScript + mediasoup-client.js (ESM). No build step — served directly by nginx or hub32api static file handler. Each student screen is a `<video>` element consuming the student's H.264 stream via mediasoup Consumer. Grid layout with CSS Grid. Responsive for tablet/laptop.

**Tech Stack:** HTML5, CSS3, vanilla JavaScript (ES modules), mediasoup-client.js v3, Fetch API

**Depends on:** Phase 3 (mediasoup signaling API), Phase 4 (agents streaming)

---

## File Structure

```
web/
├── index.html                    # Entry point, SPA shell
├── css/
│   ├── main.css                  # Global styles, CSS variables
│   ├── grid.css                  # Student grid layout
│   ├── controls.css              # Feature control panel
│   └── admin.css                 # Admin panel styles
├── js/
│   ├── app.js                    # Main app initialization
│   ├── api.js                    # REST API client (Fetch wrapper)
│   ├── auth.js                   # Login/logout, JWT management
│   ├── router.js                 # Client-side routing (hash-based)
│   ├── media/
│   │   ├── mediasoup-client.js   # mediasoup-client library (ESM CDN)
│   │   ├── stream-consumer.js    # Consume student streams
│   │   └── stream-manager.js     # Manage per-room stream subscriptions
│   ├── views/
│   │   ├── login.js              # Login page
│   │   ├── dashboard.js          # Main dashboard (room selector + grid)
│   │   ├── grid.js               # Student grid view
│   │   ├── controls.js           # Feature control panel
│   │   ├── admin-schools.js      # Admin: manage schools
│   │   ├── admin-locations.js    # Admin: manage locations
│   │   ├── admin-teachers.js     # Admin: manage teachers
│   │   └── admin-audit.js        # Admin: audit log viewer
│   └── components/
│       ├── student-card.js       # Single student video card
│       ├── nav.js                # Navigation bar
│       ├── toast.js              # Notification toasts
│       └── modal.js              # Modal dialogs
├── assets/
│   ├── logo.svg                  # HUB32 logo
│   └── icons/                    # Feature icons (SVG)
└── manifest.json                 # PWA manifest (optional)
```

---

### Task 1: Project Scaffold + API Client

**Files:**
- Create: `web/index.html`
- Create: `web/js/app.js`
- Create: `web/js/api.js`
- Create: `web/js/auth.js`
- Create: `web/css/main.css`

- [ ] **Step 1: Create index.html**

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>HUB32 - School Management</title>
    <link rel="stylesheet" href="css/main.css">
    <link rel="stylesheet" href="css/grid.css">
    <link rel="stylesheet" href="css/controls.css">
    <!-- Import map for bare specifier → local ESM file -->
    <script type="importmap">
    {
        "imports": {
            "mediasoup-client": "./js/media/mediasoup-client.esm.js"
        }
    }
    </script>
</head>
<body>
    <div id="app"></div>
    <script type="module" src="js/app.js"></script>
</body>
</html>
```

> **NOTE:** Download mediasoup-client ESM bundle from CDN to `web/js/media/mediasoup-client.esm.js`:
> `curl -o web/js/media/mediasoup-client.esm.js https://cdn.jsdelivr.net/npm/mediasoup-client@3/lib/index.js`
> The import map allows `import * as mediasoupClient from 'mediasoup-client'` to work without a bundler.

- [ ] **Step 2: Create api.js (Fetch wrapper)**

```javascript
// web/js/api.js
const BASE_URL = window.location.origin;

class Api {
    constructor() {
        this.token = sessionStorage.getItem('hub32_token');
    }

    setToken(token) {
        this.token = token;
        // SECURITY NOTE: sessionStorage is used instead of localStorage to
        // prevent token persistence across tabs/sessions. For stronger XSS
        // protection, consider httpOnly cookies in Phase 7 security hardening.
        sessionStorage.setItem('hub32_token', token);
    }

    clearToken() {
        this.token = null;
        sessionStorage.removeItem('hub32_token');
    }

    async request(method, path, body = null) {
        const headers = { 'Content-Type': 'application/json' };
        if (this.token) headers['Authorization'] = `Bearer ${this.token}`;

        const opts = { method, headers };
        if (body) opts.body = JSON.stringify(body);

        const res = await fetch(`${BASE_URL}${path}`, opts);
        if (res.status === 401) {
            this.clearToken();
            window.location.hash = '#/login';
            throw new Error('Unauthorized');
        }
        if (!res.ok) {
            const err = await res.json().catch(() => ({}));
            throw new Error(err.detail || err.title || `HTTP ${res.status}`);
        }
        if (res.status === 204) return null;
        return res.json();
    }

    get(path) { return this.request('GET', path); }
    post(path, body) { return this.request('POST', path, body); }
    put(path, body) { return this.request('PUT', path, body); }
    del(path) { return this.request('DELETE', path); }
}

export const api = new Api();
```

- [ ] **Step 3: Create auth.js**

```javascript
// web/js/auth.js
import { api } from './api.js';

export async function login(username, password) {
    const data = await api.post('/api/v1/auth', {
        method: 'logon', username, password
    });
    api.setToken(data.token);
    return data;
}

export async function logout() {
    try { await api.del('/api/v1/auth'); } catch {}
    api.clearToken();
}

export function isLoggedIn() {
    return !!sessionStorage.getItem('hub32_token');
}

export function getTokenPayload() {
    const token = sessionStorage.getItem('hub32_token');
    if (!token) return null;
    try {
        const payload = JSON.parse(atob(token.split('.')[1]));
        return payload;
    } catch { return null; }
}
```

- [ ] **Step 4: Create app.js (SPA router)**
- [ ] **Step 5: Create main.css with CSS variables**
- [ ] **Step 6: Commit**

---

### Task 2: Login Page

**Files:**
- Create: `web/js/views/login.js`

- [ ] **Step 1: Write login view**

```javascript
// web/js/views/login.js
import { login } from '../auth.js';

export function renderLogin(container) {
    container.innerHTML = `
        <div class="login-container">
            <h1>HUB32</h1>
            <form id="login-form">
                <input type="text" id="username" placeholder="Username" required>
                <input type="password" id="password" placeholder="Password" required>
                <button type="submit">Login</button>
                <p id="error" class="error hidden"></p>
            </form>
        </div>
    `;

    document.getElementById('login-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        const username = document.getElementById('username').value;
        const password = document.getElementById('password').value;
        try {
            await login(username, password);
            window.location.hash = '#/dashboard';
        } catch (err) {
            document.getElementById('error').textContent = err.message;
            document.getElementById('error').classList.remove('hidden');
        }
    });
}
```

- [ ] **Step 2: Test login against running server**
- [ ] **Step 3: Commit**

---

### Task 3: Dashboard + Room Selector

**Files:**
- Create: `web/js/views/dashboard.js`

- [ ] **Step 1: Write dashboard view**

Fetches assigned locations for the logged-in teacher. Displays tabs for each room. Clicking a tab loads the grid view for that room.

- [ ] **Step 2: Implement room selector**
- [ ] **Step 3: Commit**

---

### Task 4: Student Grid View (WebRTC Consumer)

**Files:**
- Create: `web/js/views/grid.js`
- Create: `web/js/components/student-card.js`
- Create: `web/js/media/stream-consumer.js`
- Create: `web/js/media/stream-manager.js`
- Create: `web/css/grid.css`

- [ ] **Step 1: Write stream-consumer.js**

```javascript
// web/js/media/stream-consumer.js
import * as mediasoupClient from 'mediasoup-client';
import { api } from '../api.js';

export class StreamConsumer {
    constructor(locationId) {
        this.locationId = locationId;
        this.device = null;
        this.transport = null;
        this.consumers = new Map(); // producerId → { consumer, videoElement }
    }

    async init() {
        // 1. Get Router RTP capabilities
        const caps = await api.get(
            `/api/v1/stream/capabilities/${this.locationId}`);

        // 2. Create mediasoup Device
        this.device = new mediasoupClient.Device();
        await this.device.load({ routerRtpCapabilities: caps });

        // 3. Create recv transport
        const transportInfo = await api.post('/api/v1/stream/transport', {
            locationId: this.locationId,
            direction: 'recv'
        });

        this.transport = this.device.createRecvTransport(transportInfo);

        this.transport.on('connect', async ({ dtlsParameters }, callback) => {
            await api.post(
                `/api/v1/stream/transport/${transportInfo.id}/connect`,
                { dtlsParameters });
            callback();
        });
    }

    async consume(producerId) {
        const consumerInfo = await api.post('/api/v1/stream/consume', {
            transportId: this.transport.id,
            producerId,
            rtpCapabilities: this.device.rtpCapabilities
        });

        const consumer = await this.transport.consume({
            id: consumerInfo.id,
            producerId: consumerInfo.producerId,
            kind: consumerInfo.kind,
            rtpParameters: consumerInfo.rtpParameters
        });

        const stream = new MediaStream([consumer.track]);
        this.consumers.set(producerId, { consumer, stream });
        return stream;
    }

    close() {
        for (const { consumer } of this.consumers.values()) {
            consumer.close();
        }
        if (this.transport) this.transport.close();
    }
}
```

- [ ] **Step 2: Write stream-manager.js**

Manages multiple StreamConsumers per room. Subscribes to new producers when agents come online.

- [ ] **Step 3: Write student-card.js component**

```javascript
// web/js/components/student-card.js
export function createStudentCard(computer) {
    const card = document.createElement('div');
    card.className = 'student-card';
    card.dataset.computerId = computer.id;
    card.innerHTML = `
        <video autoplay muted playsinline></video>
        <div class="student-info">
            <span class="hostname">${computer.hostname}</span>
            <span class="state state-${computer.state}">${computer.state}</span>
        </div>
    `;

    card.addEventListener('click', () => {
        // Toggle fullscreen view
        card.classList.toggle('fullscreen');
        // Switch to high simulcast layer when fullscreen
    });

    return card;
}
```

- [ ] **Step 4: Write grid.js view**

CSS Grid layout: auto-fill columns, 4-6 per row on desktop, 2 on tablet, 1 on phone.

- [ ] **Step 5: Write grid.css**
- [ ] **Step 6: Commit**

---

### Task 5: Feature Control Panel

**Files:**
- Create: `web/js/views/controls.js`
- Create: `web/css/controls.css`

- [ ] **Step 1: Write controls view**

Buttons: Lock All, Unlock All, Send Message, Power Off, Reboot, Open Website, Open App.

```javascript
// Selected students (checkbox on each card) or "All"
async function lockSelected(computerIds) {
    for (const id of computerIds) {
        await api.put(`/api/v1/computers/${id}/features/feat-lock-screen`,
            { operation: 'start' });
    }
}

async function sendMessage(computerIds, text) {
    for (const id of computerIds) {
        await api.put(`/api/v1/computers/${id}/features/feat-message`,
            { operation: 'start', arguments: { text } });
    }
}
```

- [ ] **Step 2: Add selection mechanism (checkbox on student cards)**
- [ ] **Step 3: Style control panel**
- [ ] **Step 4: Commit**

---

### Task 6: Admin Panel

**Files:**
- Create: `web/js/views/admin-schools.js`
- Create: `web/js/views/admin-locations.js`
- Create: `web/js/views/admin-teachers.js`
- Create: `web/js/views/admin-audit.js`
- Create: `web/css/admin.css`

- [ ] **Step 1: Admin school management (CRUD forms)**
- [ ] **Step 2: Admin location management (CRUD + assign teachers)**
- [ ] **Step 3: Admin teacher management (CRUD + password reset)**
- [ ] **Step 4: Audit log viewer (table with filters)**
- [ ] **Step 5: Commit**

---

### Task 7: Serve Static Files from hub32api or nginx

**Files:**
- Modify: `src/server/Router.cpp` — serve static files from web/ directory
- Alternative: nginx serves web/ and proxies /api/ to hub32api

- [ ] **Step 1: Option A — Add static file serving in Router.cpp**

```cpp
// In registerAll():
m_server.set_mount_point("/", cfg.webRoot.c_str());
```

Add `webRoot` field to ServerConfig.

- [ ] **Step 2: Option B — nginx config for static + proxy**

```nginx
server {
    listen 443 ssl;
    server_name hub32.school.example.com;

    location / {
        root /var/www/hub32/web;
        try_files $uri $uri/ /index.html;
    }

    location /api/ {
        proxy_pass http://127.0.0.1:11081;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
```

- [ ] **Step 3: Test full flow**
- [ ] **Step 4: Commit**

---

## Phase 5 Completion Checklist

- [ ] Login page with JWT authentication
- [ ] Room selector (teacher sees assigned rooms only)
- [ ] Student grid view with live WebRTC video
- [ ] Thumbnail mode (low simulcast layer) / fullscreen (high layer)
- [ ] Feature control: lock, message, power, open web/app
- [ ] Admin panel: schools, locations, teachers, audit log
- [ ] Responsive layout (desktop, tablet, phone)
- [ ] Zero build step — served as static files
