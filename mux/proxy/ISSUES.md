# Hydra Proxy — Open Issues

### 1. GANL Integration

- **File:** `front_door.cpp`, `back_door.cpp`
- **Tasks:**
  - Create GANL listeners for incoming connections.
  - Implement full lifecycle handling (Accept, Read, Write, Close) via GANL.
  - Bridge connections between front-door and back-door using GANL queueing.

### 2. Status Dumps

- **File:** `hydra_main.cpp:380`
- **Issue:** Implement status dump reporting to show current proxy state and throughput.

### 3. Configuration

- **File:** `config.cpp:213`
- **Issue:** Support duration strings (e.g., "24h", "7d") in the configuration parser.

### 4. Security

- **File:** `account_manager.cpp:309`
- **Issue:** Re-encrypt scroll-back buffers with new keys when account credentials change.
