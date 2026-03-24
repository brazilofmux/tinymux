# Mail Module — Open Issues

### 1. Hidden Flag Support

- **File:** `mail_mod.cpp:4536`
- **Issue:** Check Hidden/See_Hidden when the interface supports it to ensure proper visibility rules in mail queries.

### 2. Mail Expiration

- **File:** `mail_mod.cpp:5243`
- **Issue:** Implement mail expiration. This requires `DO_WHOLE_DB`, `No_Mail_Expire`, and proper time class integration.

### 3. Consistency Checks

- **File:** `mail_mod.cpp:5357`
- **Issue:** Implement mail consistency checks to detect and repair corruption or orphans in the mail database.

### 4. Module Data Ownership

- **File:** `mail_mod.cpp:5386`
- **Issue:** Enable advanced features once the mail module fully owns and manages its own data structures independent of core attribute storage.
