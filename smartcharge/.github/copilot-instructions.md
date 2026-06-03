# Copilot instructions for SmartCharge repository

This project is a small Django app that manages charging sessions and integrates with an external payment API (SePay) and MQTT-connected ESP32 charge stations. Use these notes to make focused, repo-aware changes.

- **Big picture**: Django site (`manage.py`) stores data in `db.sqlite3`. A separate standalone process (`mqtt_subscriber.py`) connects to an MQTT broker to receive device status. The web app sends MQTT messages when a payment is detected (`myapp/views.py`).

- **Key components**:
  - `mqtt_subscriber.py`: standalone MQTT listener (calls `django.setup()` so it can load models). Broker: `broker.hivemq.com`, topic: `iot/charge_station/v1/orders`.
  - `myapp/models.py`: `ChargerDevice`, `ChargerPort` (unique per device+port), and `ChargingSession` (status choices: `paid`, `assigned`, `done`).
  - `myapp/views.py`: payment flow, SEPAY integration (`SEPAY_API_URL`, `SEPAY_API_TOKEN`), endpoint to report device ports (`/api/charger/report-port/`), and assignment logic `assign_session_to_free_slot()` which uses `select_for_update()` inside a transaction.
  - Templates & static: `myapp/templates/myapp/*.html`, `myapp/static/*` used for the frontend QR/pay flow.

- **Service boundaries & dataflow**:
  1. User chooses amount on frontend → shows a QR (static image) from `myapp/templates/myapp/base.html`.
 2. Frontend polls `/api/check-payment/` which calls SePay API in `payment_check` logic in `myapp/views.py`.
 3. On detecting a payment the server creates a `ChargingSession` and tries to `assign_session_to_free_slot()`.
 4. If assigned, the server publishes an MQTT message via `paho.mqtt.publish.single()` to `iot/charge_station/v1/orders` with payload format: `"<device_id> <port_no> <amount>"`.
 5. ESP32 devices post their ports to `/api/charger/report-port/` to register devices and ports.

- **Concrete examples / important lines**:
  - Standalone MQTT script setup: see top of `mqtt_subscriber.py` where it calls `os.environ.setdefault("DJANGO_SETTINGS_MODULE", "smartcharge.settings")` then `django.setup()`.
  - MQTT publish in payment flow: in `myapp/views.py` lines where `mqtt_publish.single(MQTT_TOPIC_ORDER, payload, hostname=MQTT_BROKER, port=MQTT_PORT)` is used.
  - DB concurrency: `assign_session_to_free_slot()` locks ports with `.select_for_update()` inside `transaction.atomic()` — follow this pattern when modifying allocation logic.

- **Developer workflows**:
  - Run locally: create a virtualenv, install `Django` and `paho-mqtt`.
    - Typical commands:
      - `python -m venv .venv`
      - Windows PowerShell: `.\.venv\Scripts\Activate.ps1`
      - `pip install -r requirements.txt` (add one if missing) or `pip install django paho-mqtt requests`
      - `python manage.py migrate`
      - `python manage.py runserver` (web app)
      - `python mqtt_subscriber.py` (run MQTT subscriber standalone)
  - DB: sqlite file is `db.sqlite3` at project root.

- **Project-specific conventions**:
  - Comments and some identifiers are in Vietnamese — prefer minimal, clear edits and keep existing wording when editing text visible to maintainers.
  - Code uses `print()` for lightweight runtime logs rather than the `logging` module — if adding new long-running processes prefer to keep `print()` consistent unless you refactor logging project-wide.
  - Hardcoded secrets: `SEPAY_API_TOKEN` and broker host are in-source (in `myapp/views.py` and `mqtt_subscriber.py`) — treat these as sensitive and avoid exposing or committing new secrets.

- **Integration points to be careful about**:
  - External services: SePay (`SEPAY_API_URL`) and MQTT broker (`broker.hivemq.com`). Unit tests must mock these endpoints.
  - MQTT message format is a plain string: `"deviceID port amount"` — do not change format without coordinating with device firmware.

- **Tests & debugging**:
  - `myapp/tests.py` exists but is empty — there are no automated tests in the repo yet.
  - To debug MQTT flows, run `python mqtt_subscriber.py` in a separate terminal while exercising the web flow.
  - To inspect DB quickly, use `python manage.py dbshell` (or open `db.sqlite3` with a SQLite browser).

- **What to avoid / notes**:
  - Do not change the `ChargerPort` `unique_together` constraint unless migration and device firmware are updated.
  - Avoid flipping `DEBUG` to `False` without setting proper hosts and secrets.

If anything in these notes is unclear or you want deeper examples (e.g., sample unit tests for the payment flow, or a small MQTT integration test), tell me which section to expand and I'll iterate.
