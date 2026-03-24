# Mail Module — Open Issues

All issues resolved.

**Note:** `ExpireMail` uses `IsWizard` for exemption since the COM
interface only exposes `GetPowers` (first word). Full `No_Mail_Expire`
power support (`Powers2`, `POW_NO_MAIL_EXPIRE`) would require adding
a `GetPowers2` method to `mux_IObjectInfo`. Wizards are already
exempt via `No_Mail_Expire(c)` which includes `Wizard(c)`.
