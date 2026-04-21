# Factory Provisioning Design

This project separates manufacturing data from runtime user configuration.

## Partition layout

- `nvs`: user/runtime configuration
- `fctry`: factory provisioning data for one-device-one-QR

`factory_reset` only erases `nvs`. It does not erase `fctry`.

## Factory record

Partition: `fctry`

Namespace: `prov`

Key: `record`

Blob type: `factory_info_record_t`

Important fields:

- `device_id`: unique device identifier used by MQTT topics and status payloads
- `device_name`: optional factory default display name
- `service_name`: optional BLE provisioning service name printed on sticker
- `username`: Security 2 username encoded into QR code
- `pop`: Security 2 password encoded into QR code
- `salt` / `verifier`: Security 2 credentials derived from `username` and `pop`

## Runtime behavior

- If `fctry/record` is valid, provisioning uses factory username, pop, salt, verifier, and optional service name.
- If `service_name` is empty, firmware derives `PROV_xxxxxx` from Wi-Fi MAC.
- If factory record is missing, firmware falls back to development credentials so bring-up can continue.

## Recommended production flow

1. Read chip MAC on the production line.
2. Generate a unique `device_id`.
3. Generate unique Security 2 `username` and `pop`.
4. Generate matching `salt` and `verifier`.
5. Decide QR `name`.
If you want sticker and firmware to be fully deterministic, store `service_name`.
If you want to reduce stored fields, leave `service_name` empty and generate sticker `name` from MAC with the same `PROV_xxxxxx` rule.
6. Write `factory_info_record_t` into `fctry/prov/record`.
7. Print sticker QR with:
`{"ver":"v1","name":"...","username":"...","pop":"...","transport":"ble","network":"wifi"}`

## Notes

- Each device should have unique `username` and `pop`.
- `salt` and `verifier` must be generated from the same `username` and `pop`.
- Do not store production credentials in normal runtime NVS.
